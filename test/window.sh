#!/bin/bash
# Assembles the generated large-output source with both assemblers and compares
# the bytes.
#
# This is the check the differential corpus cannot make. Nothing in
# test/corpus or test/regress produces an output larger than 153 KB, and the
# one source that comes close is a single INCBIN with no forward references, so
# the corpus stays green whatever the output buffer does. test/gen_window.sh
# says at length what this source is built to exercise; the short version is
# that its output is several times any window, every fixup is settled long
# after its site has been written, and its only FILLBYTE is its last line.
#
#   test/window.sh                  the default size, and two sizes either side
#   test/window.sh 2800             one size
#   test/window.sh 40 80 120        several small ones, for a quick check
#
# Several sizes rather than one because the interesting failures are at
# boundaries -- an output that ends exactly on a window, a fixup whose bytes
# straddle one -- and the only cheap way to sweep those is to move the output
# size under a fixed window rather than the other way round.
#
# ZAP_WINDOW, if the build honours it, is passed through untouched, so the same
# sources can be run at a window small enough to force the flush path on every
# one of them.
set -uo pipefail

cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

REF="$ROOT/test/ref/linux_x86_64/ez80asm"
if [ ! -x "$REF" ]; then
    echo "FAIL  vendored ez80asm missing at $REF"
    exit 1
fi

# The same build the host tests use, and built here rather than reused so that
# running this on its own is one command. Sanitisers are left off: the point of
# this file is its size, and ASan on a 356 KB output through a 50,000 line
# source turns a tenth of a second into a minute.
CFLAGS=(-std=gnu11 -Wall -Wextra -fsigned-char -O1
        -include "$ROOT/test/stubs/host_types.h" -Isrc -I"$ROOT/test/stubs")
if [ -n "${ZAP_WINDOW:-}" ]; then
    CFLAGS+=(-DOUT_WINDOW="$ZAP_WINDOW")
    echo "window forced to $ZAP_WINDOW bytes"
fi
SRCS=(src/buf_reader.c src/value.c src/conv.c src/isa_table.c
      src/zap.c src/symtab.c src/scan.c src/expr.c src/macro.c
      src/directive.c src/insn.c src/object.c "$ROOT/test/stubs/agon_stubs.c")

cc "${CFLAGS[@]}" -o "$OUT/zap" "${SRCS[@]}" || exit 1

sizes=("$@")
if [ "${#sizes[@]}" -eq 0 ]; then
    sizes=(2799 2800 2801)
fi

status=0
for blocks in "${sizes[@]}"; do
    src="$OUT/window_$blocks.s"
    test/gen_window.sh "$blocks" > "$src" || exit 1

    rm -f "$OUT/ref.bin" "$OUT/zap.bin"
    (cd "$OUT" && "$REF" "$src" "$OUT/ref.bin" > /dev/null 2>&1) || true
    # -ez80, for the reason test/corpus.sh gives: the two disagree about
    # operator precedence on purpose, and this is a compatibility check.
    "$OUT/zap" -c -ez80 "$src" "$OUT/zap.bin" > /dev/null 2>&1 || true

    if [ ! -f "$OUT/ref.bin" ]; then
        echo "FAIL  $blocks blocks: the reference would not assemble it"
        status=1
        continue
    fi
    if [ ! -f "$OUT/zap.bin" ]; then
        echo "FAIL  $blocks blocks: zap would not assemble it"
        status=1
        continue
    fi

    rn=$(wc -c < "$OUT/ref.bin")
    zn=$(wc -c < "$OUT/zap.bin")
    if cmp -s "$OUT/ref.bin" "$OUT/zap.bin"; then
        echo "PASS  $blocks blocks: $rn bytes, identical to ez80asm"
    else
        echo "FAIL  $blocks blocks: zap $zn bytes, ez80asm $rn bytes"
        cmp -l "$OUT/ref.bin" "$OUT/zap.bin" 2>/dev/null | head -5 \
            | sed 's/^/      first differences at byte /'
        status=1
    fi
done

exit "$status"
