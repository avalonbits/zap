#!/bin/bash
# Differential corpus runner.
#
# Assembles every source in AgonPlatform/agon-ez80asm's test suite with both
# zap and ez80asm and compares the bytes. That is a stronger check than the
# .expect files alone: only 70 of the 247 in-scope sources ship an expected
# binary, and the divergence where .org fills its gap eagerly was in one of the
# other 177.
#
# Usage: test/corpus.sh <path-to-agon-ez80asm-checkout>
#
# The reference is not vendored here. Clone and build it:
#   git clone https://github.com/AgonPlatform/agon-ez80asm
#   cd agon-ez80asm && make
set -uo pipefail

REF="${1:-}"
if [ -z "$REF" ] || [ ! -x "$REF/bin/ez80asm" ]; then
    echo "usage: $0 <path-to-agon-ez80asm>  (with bin/ez80asm built)" >&2
    exit 2
fi

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

echo "-----"
echo "$total sources compared"
echo "  $same produced identical bytes"
echo "  $rejected rejected by both"
echo "  $differ disagreed"
exit $([ "$differ" -eq 0 ] && echo 0 || echo 1)
