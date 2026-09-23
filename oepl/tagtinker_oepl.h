/*
 * OpenEPaperLink (OEPL) — owner-controlled integration, request shaping only.
 *
 * The Flipper has no IP stack. This module BUILDS the HTTP request that the
 * OEPL access point documents (POST /imgupload, multipart/form-data with
 * fields `mac`, `dither`, `file`) and manages the owner's config + tag
 * allow-list. The request bytes are carried to the AP over the LAN by the
 * ESP32 WiFi bridge (Phase 2 — see docs/oepl-integration-plan.md). Nothing in
 * this module opens a socket, transmits, or touches a radio.
 *
 * Safety model:
 *   - Only tags on the owner-maintained allow-list (APP_DATA/oepl_tags.txt)
 *     can be targeted; every request build is gated by tagtinker_oepl_is_allowed().
 *   - No proprietary retail-ESL protocol, no pairing, no radio, no discovery
 *     probing. The owner names their AP and lists their own tags.
 *   - No secrets compiled in: AP host/port and an optional token live in
 *     APP_DATA/oepl.conf, created by the owner, read at runtime.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef TAGTINKER_OEPL_H
#define TAGTINKER_OEPL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define TAGTINKER_OEPL_HOST_LEN  63
#define TAGTINKER_OEPL_TOKEN_LEN 63
#define TAGTINKER_OEPL_MAC_LEN   16 /* 8 bytes -> 16 hex chars */
#define TAGTINKER_OEPL_MAX_TAGS  32

typedef struct {
    char     host[TAGTINKER_OEPL_HOST_LEN + 1];
    uint16_t port; /* default 80 */
    char     token[TAGTINKER_OEPL_TOKEN_LEN + 1]; /* "" when the AP is open */
    bool     valid; /* true once host is non-empty */
} TagTinkerOeplConfig;

typedef struct {
    char    mac[TAGTINKER_OEPL_MAX_TAGS][TAGTINKER_OEPL_MAC_LEN + 1];
    uint8_t count;
} TagTinkerOeplAllowList;

typedef enum {
    TagTinkerOeplOk = 0,
    TagTinkerOeplErrNoConfig,
    TagTinkerOeplErrBadMac,
    TagTinkerOeplErrNotAllowed,
    TagTinkerOeplErrTooBig,
} TagTinkerOeplResult;

/* ---- Pure helpers (no Flipper deps; unit-testable off-device) ----------- */

/* Normalize a MAC: drop ':' '-' ' ' separators, upper-case, validate hex.
 * Accepts exactly 12 or 16 hex digits. Writes a NUL-terminated result to
 * `out` (needs TAGTINKER_OEPL_MAC_LEN+1 bytes). Returns false on bad input. */
bool tagtinker_oepl_mac_normalize(const char* in, char* out, size_t out_size);

/* True when `mac` (after normalization) is present in the allow-list. */
bool tagtinker_oepl_is_allowed(const TagTinkerOeplAllowList* list, const char* mac);

/* Build the POST /imgupload request for an allow-listed tag.
 *
 * Produces two byte ranges around the (streamed, not included) image body:
 *   header_out  = HTTP request line + headers + the mac/dither form parts +
 *                 the file part's own header, i.e. everything BEFORE the image.
 *   trailer_out = the closing multipart boundary that follows the image.
 * and reports content_length (multipart body only: parts + file part header +
 * file_len + trailer). The caller/bridge streams file_len image bytes between
 * header_out and trailer_out.
 *
 * Returns ErrNoConfig / ErrBadMac / ErrNotAllowed / ErrTooBig as appropriate.
 * Builds nothing for a MAC that is not on the allow-list. */
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
    uint32_t* content_length);

/* ---- Storage-backed loaders (Flipper) ----------------------------------- */

/* Load APP_DATA/oepl.conf ("host=", "port=", "token=" lines). Missing file =>
 * cfg->valid == false. Never logs the token. */
void tagtinker_oepl_config_load(TagTinkerOeplConfig* cfg);

/* Load APP_DATA/oepl_tags.txt (one MAC per line; '#' comments allowed). */
void tagtinker_oepl_allowlist_load(TagTinkerOeplAllowList* list);

#endif /* TAGTINKER_OEPL_H */
