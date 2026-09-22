/*
 * TagTinker — ESL NFC tag decoder
 *
 * Reads the NDEF URI from a Mifare Ultralight / NTAG tag and decodes the
 * Pricer ESL id in its last path segment into the 17-character barcode
 * format used by TagTinker.
 *
 * Tags from other ESL vendors carry a URI too, so the host is also matched
 * against a small table to name the hardware. That is identification only:
 * nothing here transmits, and a named vendor is still a tag this app cannot
 * drive.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>

/* Longest NDEF URI body (scheme prefix excluded) we keep. */
#define TAGTINKER_NFC_URL_LEN 96

/* Longest host we extract from that body. */
#define TAGTINKER_NFC_HOST_LEN 64

/* Extract the NDEF URI body (without the "https://" style prefix) from a tag.
 * Walks the TLV area so lock/memory-control TLVs before the NDEF TLV are
 * skipped, and reads as many pages as the record actually spans.
 *
 * If truncated is non-NULL it is set when the record did not fit in url_size,
 * or ran past the pages the poller managed to read. The host is always at the
 * front of the body, so a truncated URL is still safe to identify a vendor
 * from; only the trailing path an ESL id lives in is lost. */
bool tagtinker_nfc_extract_url(
    const MfUltralightData* mfu_data,
    char* url,
    size_t url_size,
    bool* truncated);

/* Decode the Pricer ESL id in the URL's last path segment into a barcode. */
bool tagtinker_nfc_decode_url(const char* url, char barcode[18]);

/* Convenience: extract the URL from the tag and decode it in one call. */
bool tagtinker_nfc_decode_barcode(const MfUltralightData* mfu_data, char barcode[18]);

/* ---- Vendor identification ---------------------------------------------- */

typedef enum {
    TagTinkerNfcVendorUnknown = 0,
    TagTinkerNfcVendorSesImagotag,
} TagTinkerNfcVendor;

typedef struct {
    /* Matched against the URL's host, case-insensitively, on a DNS label
     * boundary: "imagotag.com" matches "nfc.imagotag.com" and "imagotag.com"
     * but not "notimagotag.com". */
    const char* host_suffix;
    TagTinkerNfcVendor vendor;
    const char* name; /* popup header, kept short enough for the 128px screen */
    const char* reason; /* popup body, may contain one '\n' */
} TagTinkerNfcVendorEntry;

/* Copy the host out of a URI body that has had its scheme prefix stripped.
 * Stops at the first '/', '?' or '#', drops any "user@" prefix, and cuts a
 * ":port" suffix. Returns false when there is no plausible host. */
bool tagtinker_nfc_url_host(const char* url, char* host, size_t host_size);

/* Identify the vendor behind a scanned URL. NULL when unrecognised.
 * The returned entry is a pointer into a static const table, so it stays
 * valid for the life of the app and is safe to hand between threads. */
const TagTinkerNfcVendorEntry* tagtinker_nfc_identify_vendor(const char* url);
