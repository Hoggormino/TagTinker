/*
 * OpenEPaperLink (OEPL) integration — implementation.
 * See tagtinker_oepl.h for the safety model. Request shaping only; no I/O
 * to any tag and no radio. SPDX-License-Identifier: GPL-3.0-only
 */
#include "tagtinker_oepl.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>
#include <stdio.h>

/* Fixed multipart boundary. A constant is fine for a one-shot client and keeps
 * the request deterministic (and therefore testable). */
#define OEPL_BOUNDARY "----TagTinkerOEPLb0undary"

/* ---- Pure helpers -------------------------------------------------------- */

static int hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool tagtinker_oepl_mac_normalize(const char* in, char* out, size_t out_size) {
    if(!in || !out || out_size < (TAGTINKER_OEPL_MAC_LEN + 1)) return false;
    size_t n = 0;
    for(const char* p = in; *p; p++) {
        char c = *p;
        if(c == ':' || c == '-' || c == ' ') continue; /* separators */
        if(hex_val(c) < 0) return false; /* any other non-hex char is invalid */
        if(n >= TAGTINKER_OEPL_MAC_LEN) return false; /* too long */
        out[n++] = (char)((c >= 'a' && c <= 'f') ? (c - 32) : c); /* upper-case */
    }
    if(n != 12 && n != 16) return false;
    out[n] = '\0';
    return true;
}

bool tagtinker_oepl_is_allowed(const TagTinkerOeplAllowList* list, const char* mac) {
    if(!list || !mac) return false;
    char norm[TAGTINKER_OEPL_MAC_LEN + 1];
    if(!tagtinker_oepl_mac_normalize(mac, norm, sizeof(norm))) return false;
    for(uint8_t i = 0; i < list->count; i++) {
        if(strcmp(list->mac[i], norm) == 0) return true;
    }
    return false;
}

TagTinkerOeplResult tagtinker_oepl_build_imgupload_request(
    const TagTinkerOeplConfig* cfg,
    const TagTinkerOeplAllowList* allow,
    const char* mac,
    bool dither,
    uint32_t file_len,
    char* header_out,
    size_t header_size,
    size_t* header_len,
    char* trailer_out,
    size_t trailer_size,
    size_t* trailer_len,
    uint32_t* content_length) {
    if(!cfg || !cfg->valid) return TagTinkerOeplErrNoConfig;
    if(!header_out || !trailer_out || !content_length) return TagTinkerOeplErrTooBig;

    char norm[TAGTINKER_OEPL_MAC_LEN + 1];
    if(!tagtinker_oepl_mac_normalize(mac, norm, sizeof(norm))) return TagTinkerOeplErrBadMac;
    if(!tagtinker_oepl_is_allowed(allow, norm)) return TagTinkerOeplErrNotAllowed;

    /* 1) Multipart body preamble: the two form parts + the file part header. */
    char preamble[512];
    int pl = snprintf(
        preamble,
        sizeof(preamble),
        "--" OEPL_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"mac\"\r\n\r\n"
        "%s\r\n"
        "--" OEPL_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"dither\"\r\n\r\n"
        "%d\r\n"
        "--" OEPL_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"tag.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        norm,
        dither ? 1 : 0);
    if(pl < 0 || (size_t)pl >= sizeof(preamble)) return TagTinkerOeplErrTooBig;

    /* 2) Trailer that follows the streamed image body. */
    int tl = snprintf(trailer_out, trailer_size, "\r\n--" OEPL_BOUNDARY "--\r\n");
    if(tl < 0 || (size_t)tl >= trailer_size) return TagTinkerOeplErrTooBig;

    /* 3) Content-Length is the whole multipart body: preamble + image + trailer. */
    uint32_t clen = (uint32_t)pl + file_len + (uint32_t)tl;

    /* 4) Full header block, then append the preamble. */
    int hl;
    if(cfg->token[0]) {
        hl = snprintf(
            header_out,
            header_size,
            "POST /imgupload HTTP/1.1\r\n"
            "Host: %s:%u\r\n"
            "User-Agent: TagTinker\r\n"
            "Authorization: Bearer %s\r\n"
            "Content-Type: multipart/form-data; boundary=" OEPL_BOUNDARY "\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n\r\n"
            "%s",
            cfg->host,
            cfg->port,
            cfg->token,
            (unsigned long)clen,
            preamble);
    } else {
        hl = snprintf(
            header_out,
            header_size,
            "POST /imgupload HTTP/1.1\r\n"
            "Host: %s:%u\r\n"
            "User-Agent: TagTinker\r\n"
            "Content-Type: multipart/form-data; boundary=" OEPL_BOUNDARY "\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n\r\n"
            "%s",
            cfg->host,
            cfg->port,
            (unsigned long)clen,
            preamble);
    }
    if(hl < 0 || (size_t)hl >= header_size) return TagTinkerOeplErrTooBig;

    if(header_len) *header_len = (size_t)hl;
    if(trailer_len) *trailer_len = (size_t)tl;
    *content_length = clen;
    return TagTinkerOeplOk;
}

/* ---- Storage-backed loaders --------------------------------------------- */

/* Read a whole small file into buf (NUL-terminated). Returns bytes read, or 0. */
static size_t oepl_read_file(const char* path, char* buf, size_t buf_size) {
    if(buf_size == 0) return 0;
    buf[0] = '\0';
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    size_t n = 0;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t r = storage_file_read(f, buf, buf_size - 1);
        n = (r < buf_size) ? r : (buf_size - 1);
        buf[n] = '\0';
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    return n;
}

/* Copy a line's value (after '='), trimming trailing CR/LF/space, bounded. */
static void oepl_copy_value(const char* src, char* dst, size_t dst_size) {
    size_t n = 0;
    while(src[n] && src[n] != '\r' && src[n] != '\n' && n + 1 < dst_size) {
        dst[n] = src[n];
        n++;
    }
    while(n > 0 && dst[n - 1] == ' ') n--; /* trim trailing spaces */
    dst[n] = '\0';
}

void tagtinker_oepl_config_load(TagTinkerOeplConfig* cfg) {
    if(!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = 80;

    char buf[512];
    if(oepl_read_file(APP_DATA_PATH("oepl.conf"), buf, sizeof(buf)) == 0) return;

    /* Line-oriented: host=..., port=..., token=... */
    char* line = buf;
    while(line && *line) {
        char* nl = strpbrk(line, "\r\n");
        if(nl) *nl = '\0';
        while(*line == ' ') line++;
        if(*line && *line != '#') {
            if(strncmp(line, "host=", 5) == 0) {
                oepl_copy_value(line + 5, cfg->host, sizeof(cfg->host));
            } else if(strncmp(line, "port=", 5) == 0) {
                int p = atoi(line + 5);
                if(p > 0 && p <= 65535) cfg->port = (uint16_t)p;
            } else if(strncmp(line, "token=", 6) == 0) {
                oepl_copy_value(line + 6, cfg->token, sizeof(cfg->token));
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    cfg->valid = cfg->host[0] != '\0';
}

void tagtinker_oepl_allowlist_load(TagTinkerOeplAllowList* list) {
    if(!list) return;
    memset(list, 0, sizeof(*list));

    char buf[1024];
    if(oepl_read_file(APP_DATA_PATH("oepl_tags.txt"), buf, sizeof(buf)) == 0) return;

    char* line = buf;
    while(line && *line && list->count < TAGTINKER_OEPL_MAX_TAGS) {
        char* nl = strpbrk(line, "\r\n");
        if(nl) *nl = '\0';
        while(*line == ' ') line++;
        if(*line && *line != '#') {
            char norm[TAGTINKER_OEPL_MAC_LEN + 1];
            if(tagtinker_oepl_mac_normalize(line, norm, sizeof(norm))) {
                /* de-dup */
                bool dup = false;
                for(uint8_t i = 0; i < list->count; i++) {
                    if(strcmp(list->mac[i], norm) == 0) {
                        dup = true;
                        break;
                    }
                }
                if(!dup) {
                    strncpy(list->mac[list->count], norm, TAGTINKER_OEPL_MAC_LEN + 1);
                    list->count++;
                }
            }
        }
        line = nl ? nl + 1 : NULL;
    }
}
