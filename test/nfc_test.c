/*
 * Host unit test for the pure URL-host matcher in the NFC module.
 *
 * Compiles the REAL nfc/tagtinker_nfc.c off-device against the minimal
 * mf_ultralight stub in test/stubs, and exercises tagtinker_nfc_url_host_is()
 * — the function that decides whether a tag's link points at a given host
 * (used to classify nfc.imagotag.com links). Pure string logic: no tag data,
 * no SDK, no radio. Run via test/run_nfc_test.sh. Exit code 0 = all pass.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "tagtinker_nfc.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(name, cond)                                \
    do {                                                 \
        int ok = (cond);                                 \
        if(!ok) fails++;                                 \
        printf("  %s %s\n", ok ? "ok  " : "FAIL", name); \
    } while(0)

#define HOST "nfc.imagotag.com"

int main(void) {
    printf("url_host_is: guards:\n");
    CHECK("NULL url -> false", !tagtinker_nfc_url_host_is(NULL, HOST));
    CHECK("NULL host -> false", !tagtinker_nfc_url_host_is("nfc.imagotag.com", NULL));
    CHECK("empty host -> false", !tagtinker_nfc_url_host_is("nfc.imagotag.com", ""));

    printf("url_host_is: scheme-less (NDEF body) matches:\n");
    CHECK("host + path", tagtinker_nfc_url_host_is("nfc.imagotag.com/0197ABC", HOST));
    CHECK("host exact (end of string)", tagtinker_nfc_url_host_is("nfc.imagotag.com", HOST));
    CHECK("host + query", tagtinker_nfc_url_host_is("nfc.imagotag.com?x=1", HOST));
    CHECK("host + port", tagtinker_nfc_url_host_is("nfc.imagotag.com:8080/x", HOST));
    CHECK("host + fragment", tagtinker_nfc_url_host_is("nfc.imagotag.com#frag", HOST));

    printf("url_host_is: case-insensitive:\n");
    CHECK("upper/mixed case host", tagtinker_nfc_url_host_is("NFC.ImagoTag.COM/x", HOST));

    printf("url_host_is: scheme stripping:\n");
    CHECK("https:// prefix", tagtinker_nfc_url_host_is("https://nfc.imagotag.com/x", HOST));
    CHECK("http:// prefix, no path", tagtinker_nfc_url_host_is("http://nfc.imagotag.com", HOST));

    printf("url_host_is: rejections:\n");
    CHECK("different host", !tagtinker_nfc_url_host_is("evil.com/x", HOST));
    CHECK("unrelated host", !tagtinker_nfc_url_host_is("example.org", HOST));
    /* Boundary check: a longer host that merely starts with HOST must not match. */
    CHECK(
        "host is a prefix of a longer label",
        !tagtinker_nfc_url_host_is("nfc.imagotag.company/x", HOST));
    /* "://" after the first '/' is part of the path, not a scheme, so no strip. */
    CHECK(
        "path containing :// still matches host",
        tagtinker_nfc_url_host_is("nfc.imagotag.com/go/redirect://evil", HOST));

    printf("url_host: extraction:\n");
    {
        char h[TAGTINKER_NFC_HOST_LEN + 1];

        CHECK(
            "strips https scheme + path",
            tagtinker_nfc_url_host("https://nfc.imagotag.com/0197ABC", h, sizeof(h)) &&
                !strcmp(h, "nfc.imagotag.com"));
        CHECK(
            "bare host",
            tagtinker_nfc_url_host("nfc.imagotag.com", h, sizeof(h)) &&
                !strcmp(h, "nfc.imagotag.com"));
        CHECK(
            "stops at port",
            tagtinker_nfc_url_host("nfc.imagotag.com:8080/x", h, sizeof(h)) &&
                !strcmp(h, "nfc.imagotag.com"));
        CHECK(
            "other host + query",
            tagtinker_nfc_url_host("http://example.org/foo?bar", h, sizeof(h)) &&
                !strcmp(h, "example.org"));

        /* Bad args / empty host: false and out cleared. */
        CHECK("NULL url -> false, out empty", !tagtinker_nfc_url_host(NULL, h, sizeof(h)) && h[0] == '\0');
        CHECK("zero out_size -> false", !tagtinker_nfc_url_host("nfc.imagotag.com", h, 0));
        CHECK("leading slash = empty host -> false", !tagtinker_nfc_url_host("/path", h, sizeof(h)));

        /* Truncation: a host longer than TAGTINKER_NFC_HOST_LEN is capped. */
        char longurl[64];
        for(size_t i = 0; i < 40; i++) longurl[i] = 'a';
        longurl[40] = '/';
        longurl[41] = 'x';
        longurl[42] = '\0';
        CHECK(
            "over-long host capped to TAGTINKER_NFC_HOST_LEN",
            tagtinker_nfc_url_host(longurl, h, sizeof(h)) &&
                strlen(h) == TAGTINKER_NFC_HOST_LEN);

        /* A small caller buffer truncates without overrun. */
        char small[5];
        CHECK(
            "small buffer truncates to out_size-1",
            tagtinker_nfc_url_host("nfc.imagotag.com", small, sizeof(small)) &&
                strlen(small) == 4 && !strcmp(small, "nfc."));
    }

    printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
