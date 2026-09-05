#!/bin/bash
# Differential corpus runner.
#
# Assembles every source in AgonPlatform/agon-ez80asm's test suite with both
# zap and ez80asm and compares the bytes. That is a stronger check than the
# .expect files alone: only 70 of the 247 in-scope sources ship an expected
# binary, and the divergence where .org fills its gap eagerly was in one of the
# other 177.
#
# A real program can be added as a second argument. The reference's tests are
# small and single-file: the largest is a few hundred lines, none has more than
# a handful of symbols, and none nests includes. Real sources are where the
# interesting failures have come from -- a case-insensitive label collision, a
# macro expansion that split a routine's locals, and a scope that was not
# restored across an include were all found by assembling BBC BASIC and Rokky,
# not by the corpus.
#
# Usage: test/corpus.sh <path-to-agon-ez80asm-checkout> [real-source.s ...]
#
# Neither is vendored here. Clone and build the reference:
#   git clone https://github.com/AgonPlatform/agon-ez80asm
#   cd agon-ez80asm && make
#
# For a real program, pass the top-level source of a checkout, e.g. BBC BASIC
# for Agon (https://github.com/breakintoprogram/agon-bbc-basic):
#   test/corpus.sh ../agon-ez80asm ../agon-bbc-basic/src/bbcbasicvez.s
set -uo pipefail

REF="${1:-}"
if [ -z "$REF" ] || [ ! -x "$REF/bin/ez80asm" ]; then
    echo "usage: $0 <path-to-agon-ez80asm> [real-source.s ...]" >&2
    exit 2
fi
shift

cd "$(dirname "$0")/.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# The host build of zap, over the same stubs the unit tests use.
cc -std=gnu11 -Wall -Wextra -fsigned-char -O1 \
   -include test/stubs/host_types.h -Isrc -Itest/stubs \
   -o "$OUT/zap" src/*.c test/stubs/agon_stubs.c || exit 1

# Two sources agreeing because neither produced anything is a weaker result
# than two producing the same bytes, so they are counted apart. A negative test
# that both reject is a pass, but calling it "assembles identically" would
# overstate what was compared.
total=0; same=0; rejected=0; differ=0

for dir in "$REF"/tests/*/; do
    name=$(basename "$dir")
    # Errors_cputype exercises -cpu selection across Z80/Z180/Z280. zap is
    # eZ80-only, so those are out of scope by design rather than deferred.
    [ "$name" = "Errors_cputype" ] && continue
    [ -d "$dir/tests" ] || continue

    for src in "$dir"/tests/*.s; do
        [ -f "$src" ] || continue
        base=$(basename "$src" .s)
        total=$((total + 1))

        rm -rf "$OUT/z" "$OUT/e"
        mkdir -p "$OUT/z" "$OUT/e"
        cp -r "$dir"/tests/* "$OUT/z/" 2>/dev/null
        cp -r "$dir"/tests/* "$OUT/e/" 2>/dev/null

        (cd "$OUT/z" && rm -f "$base.bin" && timeout 30 "$OUT/zap" "$base.s" "$base.bin" >/dev/null 2>&1)
        (cd "$OUT/e" && rm -f "$base.bin" && timeout 30 "$REF/bin/ez80asm" "$base.s" -c >/dev/null 2>&1)

        z=$([ -f "$OUT/z/$base.bin" ] && md5sum < "$OUT/z/$base.bin" || echo rejected)
        e=$([ -f "$OUT/e/$base.bin" ] && md5sum < "$OUT/e/$base.bin" || echo rejected)

        if [ "$z" = "$e" ]; then
            if [ "$z" = "rejected" ]; then
                rejected=$((rejected + 1))
            else
                same=$((same + 1))
            fi
        else
            differ=$((differ + 1))
            if [ "$z" = "rejected" ]; then
                echo "DIFFER $name/$base: zap rejected, ez80asm accepted"
            elif [ "$e" = "rejected" ]; then
                echo "DIFFER $name/$base: zap accepted, ez80asm rejected"
            else
                echo "DIFFER $name/$base: both accepted, bytes differ"
            fi
        fi
    done
done

# Whole programs, each with its include tree. Assembled in place, since an
# .include resolves relative to the working directory.
for src in "$@"; do
    if [ ! -f "$src" ]; then
        echo "SKIP $src: not found" >&2
        continue
    fi
    dir=$(cd "$(dirname "$src")" && pwd)
    base=$(basename "$src")
    stem="${base%.*}"
    total=$((total + 1))

    (cd "$dir" && rm -f "$OUT/real.z.bin" \
        && timeout 120 "$OUT/zap" "$base" "$OUT/real.z.bin" >/dev/null 2>&1)
    (cd "$dir" && rm -f "$OUT/real.e.bin" \
        && timeout 120 "$REF/bin/ez80asm" "$base" "$OUT/real.e.bin" >/dev/null 2>&1)

    z=$([ -f "$OUT/real.z.bin" ] && md5sum < "$OUT/real.z.bin" || echo rejected)
    e=$([ -f "$OUT/real.e.bin" ] && md5sum < "$OUT/real.e.bin" || echo rejected)
    zsz=$([ -f "$OUT/real.z.bin" ] && stat -c%s "$OUT/real.z.bin" || echo 0)

    if [ "$z" = "$e" ]; then
        if [ "$z" = "rejected" ]; then
            rejected=$((rejected + 1))
            echo "  $stem: rejected by both"
        else
            same=$((same + 1))
            echo "  $stem: identical, $zsz bytes"
        fi
    else
        differ=$((differ + 1))
        if [ "$z" = "rejected" ]; then
            echo "DIFFER $stem: zap rejected, ez80asm accepted"
        elif [ "$e" = "rejected" ]; then
            echo "DIFFER $stem: zap accepted, ez80asm rejected"
        else
            echo "DIFFER $stem: both accepted, bytes differ"
        fi
    fi
done

echo "-----"
echo "$total sources compared"
echo "  $same produced identical bytes"
echo "  $rejected rejected by both"
echo "  $differ disagreed"
exit $([ "$differ" -eq 0 ] && echo 0 || echo 1)
