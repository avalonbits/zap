#!/bin/bash
# Differential corpus runner.
#
# Assembles every source in the corpus with both zap and ez80asm and compares
# the bytes. That is a stronger check than the reference's .expect files alone:
# only 70 of the 507 sources ship an expected binary, and the divergence where
# .org fills its gap eagerly was in one of the other 437.
#
# Every directory, with nothing skipped. Errors_cputype was skipped for a long
# time as "out of scope by design" -- zap refused .CPU -- and when the
# directive was implemented, 250 of its 260 sources turned out to need nothing
# else, and the ten that did pointed at two real gaps. A runner that does not
# look at a thing cannot tell you what it would have found.
#
# Everything it needs is in the repository. The corpus is vendored in
# test/corpus and the reference assembler in test/ref, so this runs with no
# network and nothing to build first -- see test/ref/README.md. Both are MIT
# licensed, from AgonPlatform/agon-ez80asm.
#
# test/regress is zap's own, in the same shape and run in the same pass. The
# corpus is somebody else's tests and can only find a divergence somebody else
# already wrote down; that tree holds the ones found by reading the
# reference's diagnostic table instead.
#
#   test/corpus.sh
#       The whole corpus, against the vendored ez80asm for this architecture.
#
#   test/corpus.sh --regress
#       zap's own sources only -- test/regress -- which is the half that moves
#       while a change is being made, and runs in a few seconds.
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
ONLY=""

# zap's own sources, in a tree of their own.
#
# The corpus is vendored -- it is the reference's test suite, MIT licensed,
# and belongs to somebody else -- so nothing zap writes goes in it. These are
# the cases the corpus could not have: every divergence found by reading the
# reference's diagnostic table rather than by running its tests, each one
# written so that the two assemblers either produce the same bytes or refuse
# the same file. A regression in any of them shows up here as a DIFFER, in
# the runner that has to stay green, rather than only in run.sh.
#
# What is deliberately *not* here: the four differences zap keeps on purpose,
# which are in docs/DESIGN.md. A negative reservation would want
# four gigabytes of disk to compare.
REGRESS="test/regress"
EZ=""

while [ $# -gt 0 ]; do
    case "$1" in
        --ref)
            EZ="${2:-}/bin/ez80asm"
            shift 2
            ;;
        --regress)
            # zap's own tree only, which is the one that moves while a change
            # is being made. The vendored corpus is the slow half and does not
            # need re-running to see whether a new case bites.
            ONLY=regress
            shift
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
# runner reads "not found" as "ez80asm rejected it" -- every source reported
# as a divergence with no error anywhere.
EZ=$(cd "$(dirname "$EZ")" && pwd)/$(basename "$EZ")

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# The host build of zap, over the same stubs the unit tests use.
cc -std=gnu11 -Wall -Wextra -fsigned-char -O1 \
   -include test/stubs/host_types.h -Isrc -Itest/stubs \
   -o "$OUT/zap" src/*.c test/stubs/agon_stubs.c || exit 1

# -ez80 on every run below, because this comparison *is* the compatibility
# claim. zap's default gives operators the precedence a reader expects and the
# reference gives them none, so `1+2*3` is 7 by default and 9 here; running the
# default against the reference would be asking two assemblers that disagree on
# purpose to agree. The same reasoning as test/run.sh's case comparison.

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
total=0; same=0; rejected=0; differ=0; ours=0

for dir in "$CORPUS"/*/ "$REGRESS"/*/; do
    name=$(basename "$dir")
    [ -d "$dir/tests" ] || continue
    case "$dir" in "$REGRESS"/*) mine=1 ;; *) mine=0 ;; esac
    [ "$ONLY" = regress ] && [ "$mine" = 0 ] && continue

    for src in "$dir"/tests/*.s; do
        [ -f "$src" ] || continue
        base=$(basename "$src" .s)
        total=$((total + 1))
        ours=$((ours + mine))

        rm -rf "$OUT/z"
        mkdir -p "$OUT/z"
        cp -r "$dir"/tests/* "$OUT/z/" 2>/dev/null

        # The listing is compared for one group of zap's own sources and no
        # others -- test/regress/listing, whose files are written to be
        # comparable. Four things a one-pass assembler cannot put in a listing
        # the way a two-pass one does, all of them in docs/DESIGN.md
        # and none of them a regression: the reference widens the line-number
        # column for the whole file when it lists an expansion, a macro body
        # loses the indentation it was written with, a forward reference shows
        # the bytes as they were emitted rather than as they were patched, and
        # a reservation's fill is listed differently again. A file in that
        # group avoids all four.
        #
        # It is worth having even so: the .lst is the reference's bytes, LF
        # with one stray CR after the header, and nothing short of comparing
        # against its own file would have caught that they were CRLF here.
        lst=0
        [ "$name" = listing ] && lst=1

        zl=""
        [ "$lst" = 1 ] && zl="-l"
        (cd "$OUT/z" && rm -f "$base.bin" "$base.lst" \
            && timeout 30 "$OUT/zap" -ez80 $zl "$base.s" "$base.bin" >/dev/null 2>&1)
        z=$([ -f "$OUT/z/$base.bin" ] && md5sum < "$OUT/z/$base.bin" | cut -d' ' -f1 || echo rejected)

        rm -rf "$OUT/e"
        mkdir -p "$OUT/e"
        cp -r "$dir"/tests/* "$OUT/e/" 2>/dev/null
        (cd "$OUT/e" && rm -f "$base.bin" "$base.lst" \
            && timeout 30 "$EZ" "$base.s" -c $zl >/dev/null 2>&1)
        e=$([ -f "$OUT/e/$base.bin" ] && md5sum < "$OUT/e/$base.bin" | cut -d' ' -f1 || echo rejected)

        if [ "$lst" = 1 ] && [ "$z" = "$e" ] && [ "$z" != rejected ]; then
            if ! cmp -s "$OUT/z/$base.lst" "$OUT/e/$base.lst"; then
                differ=$((differ + 1))
                echo "DIFFER $name/$base: bytes agree, the listing does not"
                continue
            fi
        fi

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
        && timeout 120 "$OUT/zap" -ez80 "$base" "$OUT/real.z.bin" >/dev/null 2>&1)
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
echo "$total sources compared, $ours of them zap's own"
echo "  $same produced identical bytes"
echo "  $rejected rejected by both"
echo "  $differ disagreed"
exit $([ "$differ" -eq 0 ] && echo 0 || echo 1)
