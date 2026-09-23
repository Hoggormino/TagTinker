/*
 * Host-test stub for <storage/storage.h>, backed by real stdio so the OEPL
 * config/allow-list loaders can be exercised against fixture files.
 * APP_DATA_PATH("x") resolves to "$OEPL_TEST_ROOT/x". Only the API the OEPL
 * module uses is provided. Not part of the FAP build.
 */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int _unused;
} Storage;
typedef struct {
    FILE* fp;
} File;
typedef enum { FSAM_READ } FsAccessMode;
typedef enum { FSOM_OPEN_EXISTING } FsOpenMode;

static inline const char* oepl_test_path(const char* leaf) {
    static char buf[512];
    const char* root = getenv("OEPL_TEST_ROOT");
    snprintf(buf, sizeof(buf), "%s/%s", root ? root : ".", leaf);
    return buf;
}
#define APP_DATA_PATH(p) (oepl_test_path(p))

static inline File* storage_file_alloc(Storage* s) {
    (void)s;
    return calloc(1, sizeof(File));
}
static inline void storage_file_free(File* f) {
    if(f) {
        if(f->fp) fclose(f->fp);
        free(f);
    }
}
static inline int storage_file_open(File* f, const char* path, FsAccessMode a, FsOpenMode o) {
    (void)a;
    (void)o;
    f->fp = fopen(path, "rb");
    return f->fp != NULL;
}
static inline size_t storage_file_read(File* f, void* b, size_t n) {
    return f->fp ? fread(b, 1, n, f->fp) : 0;
}
static inline void storage_file_close(File* f) {
    if(f && f->fp) {
        fclose(f->fp);
        f->fp = NULL;
    }
}
