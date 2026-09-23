/*
 * Host unit test for the OpenEPaperLink module.
 *
 * Compiles and runs the REAL oepl/tagtinker_oepl.c off-device against the stub
 * furi/storage headers in test/stubs. Run via test/run_oepl_test.sh (which sets
 * OEPL_TEST_ROOT to test/fixtures). Exit code 0 = all pass.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "tagtinker_oepl.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(name, cond)                       \
    do {                                        \
        int ok = (cond);                        \
        if(!ok) fails++;                        \
        printf("  %s %s\n", ok ? "ok  " : "FAIL", name); \
    } while(0)

int main(void) {
    char m[TAGTINKER_OEPL_MAC_LEN + 1];

    printf("mac_normalize:\n");
    CHECK(
        "colons+lower",
        tagtinker_oepl_mac_normalize("aa:bb:cc:dd:ee:ff", m, sizeof(m)) &&
            !strcmp(m, "AABBCCDDEEFF"));
    CHECK(
        "16 hex",
        tagtinker_oepl_mac_normalize("00000197E5CB3B38", m, sizeof(m)) &&
            !strcmp(m, "00000197E5CB3B38"));
    CHECK("bad char rejected", !tagtinker_oepl_mac_normalize("0197E5CB3B3G", m, sizeof(m)));
    CHECK("too short rejected", !tagtinker_oepl_mac_normalize("0197E5", m, sizeof(m)));
    CHECK("too long rejected", !tagtinker_oepl_mac_normalize("00000197E5CB3B3800", m, sizeof(m)));

    printf("config + allowlist load (from test/fixtures):\n");
    TagTinkerOeplConfig cfg;
    tagtinker_oepl_config_load(&cfg);
    CHECK("cfg valid", cfg.valid);
    CHECK("cfg host", !strcmp(cfg.host, "192.168.1.50"));
    CHECK("cfg port", cfg.port == 80);
    CHECK("cfg token empty (open ap)", cfg.token[0] == '\0');

    TagTinkerOeplAllowList al;
    tagtinker_oepl_allowlist_load(&al);
    CHECK("count after dedup+skip-garbage == 3", al.count == 3);
    CHECK("first normalized", !strcmp(al.mac[0], "0197E5CB3B38"));
    CHECK("allowed, separator-insensitive", tagtinker_oepl_is_allowed(&al, "01-97-e5-cb-3b-38"));
    CHECK("allowed (AABB.. from aa-bb.. fixture)", tagtinker_oepl_is_allowed(&al, "AABBCCDDEEFF"));
    CHECK("absent -> false", tagtinker_oepl_is_allowed(&al, "0102030405060708") == false);

    printf("build_imgupload_request:\n");
    char hdr[512], trl[64];
    size_t hl = 0, tl = 0;
    uint32_t clen = 0;
    TagTinkerOeplResult r = tagtinker_oepl_build_imgupload_request(
        &cfg, &al, "01:97:e5:cb:3b:38", true, 4096, hdr, sizeof(hdr), &hl, trl, sizeof(trl), &tl,
        &clen);
    CHECK("returns Ok", r == TagTinkerOeplOk);

    /* Content-Length must equal multipart body = preamble + file + trailer.
     * Body preamble is the part of header_out after the blank line that ends
     * the HTTP headers. */
    const char* body = strstr(hdr, "\r\n\r\n");
    body = body ? body + 4 : hdr + hl;
    size_t preamble_len = hl - (size_t)(body - hdr);
    CHECK("clen == preamble + file + trailer", clen == (uint32_t)(preamble_len + 4096 + tl));
    CHECK("mac form part", strstr(hdr, "name=\"mac\"\r\n\r\n0197E5CB3B38\r\n") != NULL);
    CHECK("dither=1 form part", strstr(hdr, "name=\"dither\"\r\n\r\n1\r\n") != NULL);
    CHECK("Content-Length header present", strstr(hdr, "Content-Length: ") != NULL);
    CHECK("no auth header on open AP", strstr(hdr, "Authorization:") == NULL);
    CHECK("trailer closes boundary", strstr(trl, "--\r\n") != NULL);

    /* Safety gate: a valid but non-allow-listed MAC is refused. */
    CHECK(
        "gate: not-allowed refused",
        tagtinker_oepl_build_imgupload_request(
            &cfg, &al, "0102030405060708", true, 10, hdr, sizeof(hdr), &hl, trl, sizeof(trl), &tl,
            &clen) == TagTinkerOeplErrNotAllowed);
    CHECK(
        "gate: bad mac refused",
        tagtinker_oepl_build_imgupload_request(
            &cfg, &al, "zzz", true, 10, hdr, sizeof(hdr), &hl, trl, sizeof(trl), &tl, &clen) ==
            TagTinkerOeplErrBadMac);
    TagTinkerOeplConfig bad;
    memset(&bad, 0, sizeof(bad));
    CHECK(
        "gate: no config refused",
        tagtinker_oepl_build_imgupload_request(
            &bad, &al, "0197E5CB3B38", true, 10, hdr, sizeof(hdr), &hl, trl, sizeof(trl), &tl,
            &clen) == TagTinkerOeplErrNoConfig);
    CHECK(
        "small header buffer -> TooBig",
        tagtinker_oepl_build_imgupload_request(
            &cfg, &al, "0197E5CB3B38", true, 10, hdr, 32, &hl, trl, sizeof(trl), &tl, &clen) ==
            TagTinkerOeplErrTooBig);

    /* Token path: Authorization header appears, dither=0 for text. */
    TagTinkerOeplConfig tok = cfg;
    strcpy(tok.token, "s3cr3t");
    tagtinker_oepl_build_imgupload_request(
        &tok, &al, "0197E5CB3B38", false, 100, hdr, sizeof(hdr), &hl, trl, sizeof(trl), &tl, &clen);
    CHECK("token header present", strstr(hdr, "Authorization: Bearer s3cr3t\r\n") != NULL);
    CHECK("dither=0 form part", strstr(hdr, "name=\"dither\"\r\n\r\n0\r\n") != NULL);

    printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
