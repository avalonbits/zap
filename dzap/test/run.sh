#!/bin/bash
# Host test runner for dzap, the same shape as test/run.sh one level up.
#
# It is deliberately the same harness rather than a second convention: same
# sanitisers, same stub headers, same PASS/FAIL lines, same "every test_*.c is
# built and run" loop. Anything learned about running zap's tests applies here
# without translation.
#
# One difference, forced by dzap being a single translation unit with its own
# main. Everything in it is static, so there is no library to link a test
# against; the tests include dzap.c and rename main out of the way. That is why
# dzap.c is not in SRCS below -- each test brings its own copy.
#
# Coverage here tracks what dzap actually supports, which is instructions,
# comments and blank lines. There are no tests for labels, expressions,
# directives, macros or includes because dzap has none of those yet; as each
# arrives its tests come with it. Tests that would only assert "this is not
# implemented" are not worth writing, so they are absent rather than skipped.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT=$(cd .. && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# Same flags as zap's runner. -fsigned-char because char is signed on the
# eZ80, and host_types.h because uint24_t is a builtin there with no header.
CFLAGS=(-std=gnu11 -Wall -Wextra -fsigned-char -g -fsanitize=address,undefined
        -include "$ROOT/test/stubs/host_types.h" -Isrc -I"$ROOT/src"
        -I"$ROOT/test/stubs")

# The shared sources dzap links, minus dzap.c itself.
SRCS=(src/buf_reader.c src/value.c src/conv.c src/isa_table.c
      "$ROOT/test/stubs/agon_stubs.c")

status=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    echo "=== $name ==="
    cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" "${SRCS[@]}"
    "$OUT/$name" || status=$?
done

# The CLI, which the unit tests cannot reach, and which has to keep printing
# what ez80asm prints: the timing line exists to be compared against the
# reference's and a format that drifts stops being comparable.
echo "=== test_cli ==="
cli_check() {
    if [ "$2" = "$3" ]; then
        echo "PASS  $1"
    else
        echo "FAIL  $1: got '$2', want '$3'"
        status=1
    fi
}

cc "${CFLAGS[@]}" -o "$OUT/dzap" src/dzap.c "${SRCS[@]}"
printf '  nop\n  ret\n' > "$OUT/ok.s"
printf '  ld a,\n' > "$OUT/bad.s"

out=$("$OUT/dzap" "$OUT/ok.s" "$OUT/ok.bin" 2>&1 | tr -d '\r')
cli_check "assembling line"  "$(printf '%s' "$out" | grep -c '^Assembling ')" 1
cli_check "wrote line"       "$(printf '%s' "$out" | grep -c '^Wrote .*, 2 bytes')" 1
cli_check "timing line matches the reference's format" \
    "$(printf '%s' "$out" | grep -cE '^Done in [0-9]+\.[0-9][0-9] seconds$')" 1

bad=$("$OUT/dzap" "$OUT/bad.s" "$OUT/bad.bin" 2>&1 | tr -d '\r' || true)
cli_check "no timing on failure" "$(printf '%s' "$bad" | grep -c '^Done in ')" 0

# mnemonic_of compares without checking the length, which is only safe while
# every name in a bucket has the same length. build_tables says so if that ever
# stops being true; nothing else would notice until an instruction assembled as
# a different one.
cli_check "no two mnemonics share a bucket" \
    "$(printf '%s' "$out" | grep -c 'share a bucket')" 0

# The mode groups are held in a fixed table, sized to the table that exists. An
# overflow would silently drop the groups that did not fit and every mnemonic
# after it would stop matching anything, so build_tables says so instead.
cli_check "the mode groups fit their table" \
    "$(printf '%s' "$out" | grep -c 'mode groups')" 0
cli_check "failure is reported"  "$(printf '%s' "$bad" | grep -c 'line 1:')" 1

# The reference itself, on everything in test/cases. Unit tests pin the cases a
# refactor is likely to break; this pins the whole of what dzap claims to do
# against the assembler it has to agree with, so a case nobody thought to write
# a unit test for still cannot drift.
echo "=== test_reference ==="
REF="$ROOT/test/ref/linux_x86_64/ez80asm"
if [ ! -x "$REF" ]; then
    echo "FAIL  vendored ez80asm missing at $REF"
    status=1
else
    for src in test/cases/*.s; do
        rm -f "$OUT/ref.bin" "$OUT/dz.bin"
        # Both are allowed to fail; a failure shows up as a missing or
        # differing output below. Without the guards `set -e` would end the
        # run at the first one and the cases after it would never be
        # reported at all.
        "$REF" "$src" "$OUT/ref.bin" > /dev/null 2>&1 || true
        "$OUT/dzap" "$src" "$OUT/dz.bin" > /dev/null 2>&1 || true
        if [ -f "$OUT/ref.bin" ] && cmp -s "$OUT/ref.bin" "$OUT/dz.bin"; then
            echo "PASS  $(basename "$src") matches ez80asm"
        else
            echo "FAIL  $(basename "$src") differs from ez80asm"
            status=1
        fi
    done
fi

exit $status
