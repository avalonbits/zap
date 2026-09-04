#!/bin/bash
# Host test runner. Builds the real src/*.c against the stub agon headers in
# test/stubs and runs the resulting binaries natively.
#
# These are host tests, not target tests: they cover the parts of zap that are
# independent of the eZ80 (lexing, number conversion, the symbol table, code
# selection). `char` is 1 byte and signed on both targets -- -fsigned-char makes
# that explicit rather than relying on the host default -- so truncation
# behaviour matches the device. uint24_t is a builtin on the eZ80 compiler and
# has no header to come from, so host_types.h is force-included ahead of every
# translation unit. Anything that depends on the eZ80's 3-byte int, or on real
# MOS behaviour, still needs the emulator.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# ASan catches the out-of-bounds writes that the lexer's unbounded token buffer
# can produce; UBSan catches the signed overflow the number conversion can hit.
CFLAGS=(-std=gnu11 -Wall -Wextra -fsigned-char -g -fsanitize=address,undefined
        -include test/stubs/host_types.h -Isrc -Itest/stubs)

# Everything except main.c, which owns the CLI entry point. The library is what
# is under test; the zap binary is a consumer of it.
SRCS=(src/lexer.c src/buf_reader.c src/hash_table.c src/lex_types.c src/value.c src/expr.c
      src/conv.c src/label_stack.c src/parser.c src/instruction_parser.c
      test/stubs/agon_stubs.c)

status=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    echo "=== $name ==="
    cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" "${SRCS[@]}"
    "$OUT/$name" || status=$?
done

exit $status
