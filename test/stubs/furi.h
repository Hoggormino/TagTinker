/*
 * Host-test stub for <furi.h>. Only the symbols the OEPL module uses.
 * Lets the REAL oepl/tagtinker_oepl.c compile and run off-device (CI / laptop).
 * Not used by the FAP build — ufbt compiles only the sources in application.fam.
 */
#pragma once
#include <stdlib.h>

#define RECORD_STORAGE "storage"

static inline void* furi_record_open(const char* name) {
    (void)name;
    return (void*)1;
}
static inline void furi_record_close(const char* name) {
    (void)name;
}
