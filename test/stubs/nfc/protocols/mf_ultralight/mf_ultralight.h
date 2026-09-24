/*
 * Minimal off-device stub of the Flipper SDK's MfUltralight data type.
 *
 * Only the members that nfc/tagtinker_nfc.c actually touches are provided:
 * `pages_read` and `page[i].data` (4-byte Ultralight/NTAG pages). This lets
 * the real module compile in the host test without the Flipper SDK. It is a
 * test fixture, not a faithful reproduction of the SDK header.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t data[4];
} MfUltralightPage;

typedef struct {
    uint16_t pages_read;
    MfUltralightPage page[256];
} MfUltralightData;
