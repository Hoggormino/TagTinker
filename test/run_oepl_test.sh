#!/bin/sh
# Build and run the OEPL host unit test. Compiles the real oepl/tagtinker_oepl.c
# against the stub headers in test/stubs. Requires a C compiler (cc/clang/gcc).
# Exit 0 = all tests pass, 1 = test failure, 2 = compile failure.
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/.." && pwd)
CC=${CC:-cc}
OUT=$(mktemp -d)
if ! "$CC" -std=c11 -Wall -Wextra -I"$DIR/stubs" -I"$ROOT/oepl" \
     "$DIR/oepl_test.c" "$ROOT/oepl/tagtinker_oepl.c" -o "$OUT/oepl_test"; then
    echo "compile failed"
    rm -rf "$OUT"
    exit 2
fi
OEPL_TEST_ROOT="$DIR/fixtures" "$OUT/oepl_test"
rc=$?
rm -rf "$OUT"
exit $rc
