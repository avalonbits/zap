#!/bin/bash
# Generates a source that is one instruction shape, repeated.
#
#   test/bench/gen_shape.sh <shape> [lines] > shape.s
#
# The per-instruction costs quoted in .internal/dzap-to-zap.md come from these.
# A file of one shape isolates what that shape costs: the difference between
# two of them is the difference between the two instructions and nothing else,
# where a mixed source only ever gives an average. It is also how the row
# scanning cost was found -- `disp` reaches the forty-third of ld's 57 rows and
# nothing else in the set reaches past the fifth.
#
# The default of 30,000 lines puts every shape between five and fifteen
# seconds, which is long enough that the centisecond clock is noise and short
# enough to iterate on. Changing a shape's text invalidates every timing taken
# with it; add a new one instead.
#
# Shapes:
#   nop     no operands at all -- the floor every instruction is built on
#   reg     register to register
#   imm8    register and a one-byte immediate
#   imm24   register and a three-byte immediate, the widest ADL takes
#   cb      a CB-prefixed bit operation with an index displacement
#   disp    an index displacement, which walks furthest through ld's rows
set -euo pipefail

SHAPE="${1:?usage: gen_shape.sh <nop|reg|imm8|imm24|cb|disp> [lines]}"
LINES="${2:-30000}"

case "$SHAPE" in
    nop)   TEXT='  nop' ;;
    reg)   TEXT='  ld a, b' ;;
    imm8)  TEXT='  ld a, 0x42' ;;
    imm24) TEXT='  ld hl, 0x123456' ;;
    cb)    TEXT='  bit 3, (iy+4)' ;;
    disp)  TEXT='  ld (ix+8), a' ;;
    *) echo "unknown shape: $SHAPE" >&2; exit 2 ;;
esac

for ((i = 0; i < LINES; i++)); do
    printf '%s\n' "$TEXT"
done
