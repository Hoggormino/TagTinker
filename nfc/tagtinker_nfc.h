/*
 * TagTinker — ESL NFC tag decoder
 *
 * Reads the NDEF URI from a Mifare Ultralight / NTAG tag and decodes the
 * Pricer ESL id in its last path segment into the 17-character barcode
 * format used by TagTinker.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>

/* Longest NDEF URI body (scheme prefix excluded) we keep. */
#define TAGTINKER_NFC_URL_LEN 96

/* Extract the NDEF URI body (without the "https://" style prefix) from a tag.
 * Walks the TLV area so lock/memory-control TLVs before the NDEF TLV are
 * skipped, and reads as many pages as the record actually spans. */
bool tagtinker_nfc_extract_url(const MfUltralightData* mfu_data, char* url, size_t url_size);

/* Decode the Pricer ESL id in the URL's last path segment into a barcode. */
bool tagtinker_nfc_decode_url(const char* url, char barcode[18]);

/* Convenience: extract the URL from the tag and decode it in one call. */
bool tagtinker_nfc_decode_barcode(const MfUltralightData* mfu_data, char barcode[18]);
