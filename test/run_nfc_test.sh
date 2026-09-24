#!/bin/sh
# Build and run the NFC host unit test. Compiles the real nfc/tagtinker_nfc.c
# against the minimal mf_ultralight stub in test/stubs and runs test/nfc_test.c.
# Requires a C compiler (cc/clang/gcc). Exit 0 = pass, 1 = failure, 2 = compile.
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/.." && pwd)
CC=${CC:-cc}
OUT=$(mktemp -d)
if ! "$CC" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -I"$DIR/stubs" -I"$ROOT/nfc" \
     "$DIR/nfc_test.c" "$ROOT/nfc/tagtinker_nfc.c" -o "$OUT/nfc_test"; then
    echo "compile failed"
    rm -rf "$OUT"
    exit 2
fi
"$OUT/nfc_test"
rc=$?
rm -rf "$OUT"
exit $rc
