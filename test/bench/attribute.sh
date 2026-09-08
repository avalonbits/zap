#!/bin/bash
# What each feature costs, measured on one binary and seven inputs.
#
#   test/bench/attribute.sh [assembler.bin]
#
# The staged builds -- -DTRUNC=n and its relatives -- answer a different
# question badly. Truncating assemble_line at stage four and at stage five
# produces two different programs: what is live across the cut changes, the
# allocator spills differently, the frame lands somewhere else, and those
# effects are the same size as the thing being measured. Two changes were made
# on the strength of that map and both were slower; the write-up is in
# .internal/performance-notes.md.
#
# This varies the *input* instead. One binary -- the one that ships -- against
# a family of sources that differ in exactly one feature, all the same size.
# Nothing is compiled away, nothing moves in the generated code, and the
# difference between two runs is the difference between two programs the
# assembler actually assembles.
#
# WHAT THE NUMBER MEANS. Every file is the same byte budget, so leaving a
# feature out fills the space with ordinary instructions instead. The delta is
# therefore what the feature costs **net of the instructions that replace
# it**, which is a little smaller than what it costs outright and is the
# figure that answers "is this worth optimising". gen_comments.sh has measured
# comments this way since it was written; this generalises it.
#
# It also means the other features get slightly denser when one is removed --
# about 2% more of everything else, from the space freed. That is inside the
# resolution of a single run and is why the table prints the delta rather than
# pretending to a per-occurrence figure it cannot support.
set -uo pipefail

cd "$(dirname "$0")/../.."

BIN="${1:-bin/zap.bin}"
if [ ! -f "$BIN" ]; then
    echo "no assembler at $BIN; run make" >&2
    exit 2
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Snapshotted, for the reason bench.sh snapshots: a `make` while this is in
# flight would otherwise measure two different binaries in one table. That has
# happened, and so has editing the source mid-run.
cp "$BIN" "$WORK/zap.bin"

FEATURES="equ macro cond assume suffix data"

# Counted on the baseline, so the table can say how many lines carry the
# feature that was removed.
count_of() {
    case "$1" in
        equ)    grep -c ' EQU ' "$WORK/base.s" ;;
        macro)  grep -cE '^  (msave|mload|msum|mwait|mtri|mrest|mg)' "$WORK/base.s" ;;
        cond)   grep -cE '^  (IF |ELSE|ENDIF)' "$WORK/base.s" ;;
        assume) grep -c 'ASSUME' "$WORK/base.s" ;;
        suffix) grep -cE '\.(lil|sis|lis|l|s) ' "$WORK/base.s" ;;
        data)   grep -cE '^  (DB|DW|DL|DS|ALIGN|ORG)' "$WORK/base.s" ;;
    esac
}

test/bench/gen_isa.sh real > "$WORK/base.s"
for f in $FEATURES; do
    ISA_OMIT="$f" test/bench/gen_isa.sh real > "$WORK/$f.s"
done

echo "one binary, seven inputs, each 256 KiB"
echo
printf '%-12s %8s %8s %8s %10s\n' SOURCE SECONDS DELTA LINES 'CYCLES/LINE'

base=$(test/bench/time-one.sh "$WORK/zap.bin" "$WORK/base.s" 2>/dev/null)
if [ -z "$base" ]; then
    echo "the baseline run produced no figure" >&2
    exit 1
fi
printf '%-12s %8s %8s %8s %10s\n' 'isa_real' "$base" '-' '-' '-'

for f in $FEATURES; do
    t=$(test/bench/time-one.sh "$WORK/zap.bin" "$WORK/$f.s" 2>/dev/null)
    n=$(count_of "$f")
    if [ -z "$t" ]; then
        printf '%-12s %8s %8s %8s %10s\n' "-$f" '-' '-' "$n" '-'
        continue
    fi
    awk -v f="-$f" -v t="$t" -v b="$base" -v n="$n" 'BEGIN {
        d = b - t
        # 18.432 MHz, and the delta is over the whole file.
        c = (n > 0) ? (d * 18432000 / n) : 0
        printf "%-12s %8.2f %8+.2f %8d %10.0f\n", f, t, -d, n, c
    }'
done
