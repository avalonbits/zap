#!/bin/bash
# Generates the pure-instruction benchmark source.
#
# Nothing but instructions: no labels, no comments, no blank lines, no
# constants, no directives. Every line maps straight to its bytes and nothing
# refers to anything else, which is the case dzap exists to measure -- and the
# case zap can be run against to see what its machinery costs when none of it
# is needed.
#
# Deterministic, like gen_synth.sh: the line at index i is a pure function of
# i, so any run reproduces any earlier one. Changing this script invalidates
# every timing taken before the change; add a new generator instead of editing
# this one.
#
#   test/bench/gen_pure.sh [lines] > pure.s
#
# The default is sized to 256 KiB, which dzap assembles in about 20 seconds --
# short enough to iterate on and long enough that the 10 ms clock is noise.
# The work is linear in the source, verified: dzap took 39.02s on 507 KB and
# 78.90s on 1049 KB, a ratio of 2.02 against a size ratio of 2.02.
#
# 2 MiB was the intent and does not fit. These instructions produce a shade
# under a fifth of a byte of output per source byte, so 2 MiB assembles to
# 376,650 bytes -- and holding that on a 512 KB machine fails, whatever the
# assembler does with the rest of its time. dzap reached line 64,727 and ran
# out. That is the memory wall reached by a program with no symbol table, no
# fixups and no bookkeeping of any kind, which is worth knowing on its own.
#
# The instruction set here is restricted to what dzap handles: registers,
# literals, indirection and an index displacement. Condition codes are in,
# suffixes and expressions are not -- an expression between literals is one of
# the features to add back and price separately, so it does not belong in the
# floor.
set -euo pipefail

LINES="${1:-23240}"

awk -v n="$LINES" '
BEGIN {
    i = 0
    op[i++] = "  nop"
    op[i++] = "  ld a, 0x42"
    op[i++] = "  ld bc, 0x1234"
    op[i++] = "  add a, b"
    op[i++] = "  inc hl"
    op[i++] = "  ld (ix+8), a"
    op[i++] = "  bit 3, (iy+4)"
    op[i++] = "  res 7, b"
    op[i++] = "  set 0, (hl)"
    op[i++] = "  rlc d"
    op[i++] = "  ld de, 0x8000"
    op[i++] = "  sbc hl, bc"
    op[i++] = "  ldir"
    op[i++] = "  push af"
    op[i++] = "  pop iy"
    op[i++] = "  ex de, hl"
    op[i++] = "  cp 0x7F"
    op[i++] = "  and 0xAA"
    op[i++] = "  or (hl)"
    op[i++] = "  xor 0x0F"
    op[i++] = "  ret nz"
    op[i++] = "  im 2"
    op[i++] = "  rst 0x18"
    op[i++] = "  out (0xFE), a"
    op[i++] = "  in a, (0xFE)"
    op[i++] = "  ld a, (0x040100)"
    op[i++] = "  ld hl, (0x040200)"
    op[i++] = "  mlt de"
    op[i++] = "  add hl, de"
    op[i++] = "  srl c"
    op[i++] = "  neg"
    op[i++] = "  scf"
    op[i++] = "  dec sp"
    op[i++] = "  ld iy, 0x0400"
    op[i++] = "  adc a, (ix+0)"
    op[i++] = "  ld a, i"
    op[i++] = "  sla e"
    op[i++] = "  ccf"
    op[i++] = "  halt"
    op[i++] = "  ld sp, hl"
    count = i

    for (k = 0; k < n; k++) {
        print op[k % count]
    }
}
' </dev/null
