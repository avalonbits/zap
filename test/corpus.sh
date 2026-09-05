#!/bin/bash
# Differential corpus runner.
#
# Assembles every source in the corpus with both zap and ez80asm and compares
# the bytes. That is a stronger check than the reference's .expect files alone:
# only 70 of the 247 in-scope sources ship an expected binary, and the
# divergence where .org fills its gap eagerly was in one of the other 177.
#
# Everything it needs is in the repository. The corpus is vendored in
# test/corpus and the reference assembler in test/ref, so this runs with no
# network and nothing to build first -- see test/ref/README.md. Both are MIT
# licensed, from AgonPlatform/agon-ez80asm.
#
#   test/corpus.sh
#       The whole corpus, against the vendored ez80asm for this architecture.
#
#   test/corpus.sh <real-source.s ...>
#       Whole programs as well, each assembled in place with its include tree.
#       These are not vendored, being other people's repositories.
#
#   test/corpus.sh --ref <path-to-agon-ez80asm> [real-source.s ...]
#       Against a different build of the reference, which is what to use when
#       checking whether a divergence is zap's or a change in ez80asm.
#
# Real programs are where the interesting failures have come from: a
# case-insensitive label collision, a macro expansion that split a routine's
# locals, and a scope that was not restored across an include were all found by
# assembling BBC BASIC and Rokky, not by this corpus. Keep running them.
#
#   test/corpus.sh ../agon-bbc-basic/src/bbcbasicvez.s ../rokky/rokky.s
set -uo pipefail

cd "$(dirname "$0")/.."

CORPUS="test/corpus"
EZ=""

while [ $# -gt 0 ]; do
    case "$1" in
        --ref)
            EZ="${2:-}/bin/ez80asm"
            shift 2
            ;;
        -h|--help)
            sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            break
            ;;
    esac
done

# The vendored reference for this machine. Falling back to whatever ez80asm is
# on PATH would compare against an unknown version and quietly report someone
# else's divergence as zap's, so an unknown architecture is an error with a way
# out rather than a guess.
if [ -z "$EZ" ]; then
    case "$(uname -m)" in
        x86_64|amd64)   EZ="test/ref/linux_x86_64/ez80asm" ;;
        aarch64|arm64)  EZ="test/ref/linux_aarch64/ez80asm" ;;
        *)
            echo "no vendored ez80asm for $(uname -m); pass --ref <path-to-a-build>" >&2
            exit 2
            ;;
    esac
fi
if [ ! -x "$EZ" ]; then
    echo "reference assembler not executable: $EZ" >&2
    exit 2
fi

# Absolute, because every source is assembled from inside its own work
# directory. A relative path silently resolves to nothing there, and the
# runner reads "not found" as "ez80asm rejected it" -- 247 sources reported as
# divergences with no error anywhere.
EZ=$(cd "$(dirname "$EZ")" && pwd)/$(basename "$EZ")

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# The host build of zap, over the same stubs the unit tests use.
cc -std=gnu11 -Wall -Wextra -fsigned-char -O1 \
   -include test/stubs/host_types.h -Isrc -Itest/stubs \
   -o "$OUT/zap" src/*.c test/stubs/agon_stubs.c || exit 1

# Prove the reference works before trusting anything it does not produce.
#
# A missing output file is read below as "ez80asm rejected this", which is
# indistinguishable from a reference that cannot run at all -- a relative path
# resolved from inside the work directory, a binary without its interpreter, a
# stale build. Each of those reports every source in the corpus as a divergence
# and looks exactly like zap having broken. One known-good source first turns
# that into one clear message.
mkdir -p "$OUT/probe"
printf '  .assume adl=1\n  .org $40000\n  nop\n  ret\n' > "$OUT/probe/probe.s"
(cd "$OUT/probe" && timeout 30 "$EZ" probe.s -c >/dev/null 2>&1)
if [ ! -s "$OUT/probe/probe.bin" ]; then
    echo "$EZ produced no output for a source that must assemble:" >&2
    (cd "$OUT/probe" && "$EZ" probe.s -c) >&2
    echo "the reference is not working; nothing below would mean anything" >&2
    exit 2
fi

# Two sources agreeing because neither produced anything is a weaker result
# than two producing the same bytes, so they are counted apart. A negative test
# that both reject is a pass, but calling it "assembles identically" would
# overstate what was compared.
total=0; same=0; rejected=0; differ=0

for dir in "$CORPUS"/*/; do
    name=$(basename "$dir")
    # Errors_cputype exercises -cpu selection across Z80/Z180/Z280. zap is
    # eZ80-only, so those are out of scope by design rather than deferred.
    [ "$name" = "Errors_cputype" ] && continue
    [ -d "$dir/tests" ] || continue

    for src in "$dir"/tests/*.s; do
        [ -f "$src" ] || continue
        base=$(basename "$src" .s)
        total=$((total + 1))

        rm -rf "$OUT/z"
        mkdir -p "$OUT/z"
        cp -r "$dir"/tests/* "$OUT/z/" 2>/dev/null

        (cd "$OUT/z" && rm -f "$base.bin" && timeout 30 "$OUT/zap" "$base.s" "$base.bin" >/dev/null 2>&1)
        z=$([ -f "$OUT/z/$base.bin" ] && md5sum < "$OUT/z/$base.bin" | cut -d' ' -f1 || echo rejected)

        rm -rf "$OUT/e"
        mkdir -p "$OUT/e"
        cp -r "$dir"/tests/* "$OUT/e/" 2>/dev/null
        (cd "$OUT/e" && rm -f "$base.bin" && timeout 30 "$EZ" "$base.s" -c >/dev/null 2>&1)
        e=$([ -f "$OUT/e/$base.bin" ] && md5sum < "$OUT/e/$base.bin" | cut -d' ' -f1 || echo rejected)

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
        && timeout 120 "$EZ" "$base" "$OUT/real.e.bin" >/dev/null 2>&1)

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
