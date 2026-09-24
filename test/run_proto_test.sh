#!/bin/sh
# Build and run the protocol host unit test. Covers the pure geometry/layout
# helpers defined inline in protocol/tagtinker_proto.h; needs no stubs because
# the header is plain C (stdint/stddef/stdbool) with no SDK dependency.
# Requires a C compiler (cc/clang/gcc). Exit 0 = pass, 1 = failure, 2 = compile.
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/.." && pwd)
CC=${CC:-cc}
OUT=$(mktemp -d)
if ! "$CC" -std=c11 -Wall -Wextra -I"$ROOT/protocol" \
     "$DIR/proto_test.c" -o "$OUT/proto_test"; then
    echo "compile failed"
    rm -rf "$OUT"
    exit 2
fi
"$OUT/proto_test"
rc=$?
rm -rf "$OUT"
exit $rc
