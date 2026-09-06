#!/bin/bash
# Generates a 384 KiB instruction source with a chosen share of comment bytes.
#
#   test/bench/gen_comments.sh <percent> [bytes] > src.s
#
# The file is always the same size; the percentage says how much of it is
# comment rather than instruction. Raising it removes instructions, so the
# series measures two things at once: what an instruction costs, and what a
# comment costs. If comment handling were free the time would fall linearly
# with the instruction count; whatever does not fall is what skipping a comment
# costs.
#
# Both shapes are produced, because they take different paths through a lexer:
# a whole line that is nothing but a comment, and a comment following an
# instruction on the same line. They are mixed in a fixed 2:1 ratio of
# whole-line to trailing so the split does not drift between percentages.
#
# Deterministic, like the other generators: the decision at each line depends
# only on the bytes emitted so far, so any run reproduces any earlier one.
# Changing this script invalidates every timing taken with it.
set -euo pipefail

PCT="${1:?usage: gen_comments.sh <percent> [bytes]}"
TOTAL="${2:-393216}"

awk -v pct="$PCT" -v total="$TOTAL" '
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
    nop_ = i

    # A whole-line comment, and the tail added to an instruction line. Fixed
    # lengths so the byte accounting below is exact.
    whole = "; a remark occupying one whole line of the source"
    tail  = "   ; a remark after the instruction"

    want = total * pct / 100      # comment bytes wanted
    cbytes = 0                    # comment bytes emitted
    bytes = 0                     # bytes emitted
    k = 0                         # which instruction next
    w = 0                         # whole-line comments emitted, for the 2:1 mix

    while (bytes < total) {
        behind = (cbytes < want * (bytes + 1) / total)

        if (behind && w % 3 != 2) {
            # A whole line of comment.
            print whole
            bytes += length(whole) + 1
            cbytes += length(whole) + 1
            w++
        } else if (behind) {
            # An instruction carrying a comment after it.
            line = op[k % nop_] tail
            k++
            print line
            bytes += length(line) + 1
            cbytes += length(tail)
            w++
        } else {
            print op[k % nop_]
            bytes += length(op[k % nop_]) + 1
            k++
        }
    }
}
' </dev/null
