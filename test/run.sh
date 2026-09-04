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
      src/conv.c src/label_stack.c src/parser.c src/operand.c src/isa_table.c src/encode.c src/macro.c src/zap.c
      test/stubs/agon_stubs.c)

status=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    echo "=== $name ==="
    cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" "${SRCS[@]}"
    "$OUT/$name" || status=$?
done

# The CLI, which the unit tests cannot reach: main.c is left out of SRCS
# because the library is what is under test. What it reports is worth pinning
# anyway, since the timing line exists to be compared against ez80asm's and a
# format that drifts stops being comparable.
echo "=== test_cli ==="
cli_check() {
    if [ "$2" = "$3" ]; then
        echo "PASS  $1"
    else
        echo "FAIL  $1: got '$2', want '$3'"
        status=1
    fi
}

cc "${CFLAGS[@]}" -o "$OUT/zap" src/main.c "${SRCS[@]}"
printf '  .assume adl=1\n  .org $40000\n  nop\n  ret\n' > "$OUT/ok.s"
printf '  ld a,\n' > "$OUT/bad.s"

out=$("$OUT/zap" "$OUT/ok.s" "$OUT/ok.bin" 2>&1 | tr -d '\r')
cli_check "assembling line" \
    "$(printf '%s' "$out" | grep -c '^Assembling ')" 1
cli_check "wrote line" \
    "$(printf '%s' "$out" | grep -c '^Wrote .*, 2 bytes')" 1

# The reference prints "Done in 0.00 seconds"; matching it exactly is the
# point, so this pins the shape rather than the value.
cli_check "timing line matches the reference's format" \
    "$(printf '%s' "$out" | grep -cE '^Done in [0-9]+\.[0-9][0-9] seconds$')" 1

# The conversion itself is tested in test_timing.c, against known clock values.
# It cannot be tested here by assembling something and asserting it took more
# than a hundredth of a second: the workload that takes two centiseconds under
# the sanitisers takes none at -O2, so on a faster host correct behaviour would
# start failing.

# And, like the reference, no timing is reported for an assembly that failed.
bad=$("$OUT/zap" "$OUT/bad.s" "$OUT/bad.bin" 2>&1 | tr -d '\r' || true)
cli_check "no timing on failure" \
    "$(printf '%s' "$bad" | grep -c '^Done in ')" 0
cli_check "failure is reported" \
    "$(printf '%s' "$bad" | grep -c 'line 1:')" 1

exit $status
