#!/bin/bash
# Generates a source that uses labels, to price them in use.
#
#   test/bench/gen_labels.sh [bytes] > labels.s
#   test/bench/gen_labels.sh [bytes] same > labels_same.s
#   test/bench/gen_labels.sh [bytes] local > labels_local.s
#
# isa_even and isa_real contain no labels at all, so they say what labels cost
# a source that does not use them -- the test on every line, and nothing else.
# They cannot say what a lookup costs, or a fixup.
#
# A NEW FILE RATHER THAN AN EDIT TO THOSE. Changing them would invalidate every
# timing taken with them, and the label cost is wanted as a difference against
# a number that still means something.
#
# The shape is taken from the two real programs in test/corpus, counted:
#
#   11-15% of lines define a label       -- here, one every eight
#   38-62% of instruction lines refer to one
#   labels are 6-8 characters at the median, 17 at the 95th percentile
#
# Half the references point backwards and half forwards, because a forward one
# costs a fixup and a backward one does not, and a file of only one kind would
# price whichever it happened to contain.
#
# Deterministic: the line at index i is a function of i. Changing this script
# invalidates every timing taken with it.
set -euo pipefail

BYTES="${1:-262144}"
MODE="${2:-varied}"

# `same` names every label from one stem -- stem_0001 and up -- which shares a
# first character, a length, and a last character drawn from ten digits. The
# symbol key is the first character, the last and the length, so that puts
# thousands of labels in ten buckets. It is the worst case for this key and the
# reason it is worth having a name for: the first version of this generator
# produced it by accident and made labels look 6.6x more expensive than they
# are. A hash would not care. Kept so the gap can be watched rather than
# rediscovered.

awk -v want="$BYTES" -v mode="$MODE" 'BEGIN {
    # A pool of instruction lines that need no operand of their own, so the
    # mix stays about what real code looks like without dragging in the whole
    # instruction set -- isa_even and isa_real are what covers that.
    n = split("ld a,b|ld c,(hl)|add a,e|inc hl|dec b|push bc|pop de|xor a|" \
              "and 0x0F|cp 0x20|ld (hl),a|ex de,hl|sbc hl,bc|rlca|nop|" \
              "ld d,(ix+4)|or (hl)|adc a,l|res 3,c|bit 7,h", pool, "|")

    # Names built from a word list, so the first and last characters and the
    # length all vary the way they do in real code.
    #
    # THIS MATTERS MORE THAN IT LOOKS. The first version numbered one stem --
    # lbl_routine_body_0001 and up -- which shares a first character, a length
    # and a last character drawn from ten digits, so the symbol key put 2,161
    # labels in ten buckets and the file took 20.82s against 3.14s for the same
    # instructions without labels. That is not what labels cost; it is what one
    # naming style costs this key, and it is measured on its own in the
    # pathological mode below rather than being allowed to stand in for the
    # ordinary case.
    w = split("read|write|draw|move|scan|calc|emit|parse|check|reset|" \
              "flush|store|load|swap|clip|tile|sprite|sound|timer|port|" \
              "queue|stack|frame|pixel|glyph|board|piece|score|level|input", word, "|")

    # The names avoid @b, @f, @n and @p. Those two-character spellings are the
    # anonymous-label references of the reference assembler -- previous and
    # next -- whatever a local of that name would mean, so a generator using
    # @a/@b/@c produces a file the reference refuses. No apostrophes in here
    # either: the whole program is inside a single-quoted shell string.
    #
    # `local` puts the same labels through the local table instead: a global
    # every 32 lines opening a scope, three locals inside it, and the same
    # eight references per 32 lines the varied mode has -- four forward and
    # four backward, every one of them inside the scope it is written in,
    # because a local reference cannot reach out of one.
    #
    # Same line count, same instruction mix, same label density. What differs
    # is which table the labels land in, so the difference against the varied
    # mode is what local labels cost and nothing else.
    if (mode == "local") {
        size = 0; i = 0; scope = 0
        while (size < want) {
            k = i % 32
            if (k == 0)       { scope++; line = sprintf("g%04d:", scope) }
            else if (k == 8)  line = "@aa:"
            else if (k == 16) line = "@bb:"
            else if (k == 24) line = "@cc:"
            else if (k == 3)  line = "\tcall @aa"
            else if (k == 5)  line = "\tcall @aa"
            else if (k == 11) line = "\tjp @aa"
            else if (k == 13) line = "\tcall @bb"
            else if (k == 19) line = "\tjp @bb"
            else if (k == 21) line = "\tcall @cc"
            else if (k == 27) line = "\tjp @cc"
            else if (k == 29) line = "\tjp @aa"
            else              line = "\t" pool[(i % n) + 1]
            print line
            size += length(line) + 1
            i++
        }
        # Whatever scope the file ended in must have its forward references
        # landed on. Each local is emitted only if this scope had not reached
        # the line that defines it; a second definition in one scope is an
        # error, which is the point of being careful here.
        k = i % 32
        if (k > 0) {
            if (k <= 8)  print "@aa:"
            if (k <= 16) print "@bb:"
            if (k <= 24) print "@cc:"
        }
        exit 0
    }

    size = 0; i = 0; lbl = 0
    while (size < want) {
        # One definition every eight lines: 12.5%, inside the 11-15% counted.
        if (i % 8 == 0) {
            lbl++
            line = name(lbl) ":"
        } else if (i % 8 == 3 && lbl > 1) {
            # Backward: the label exists, so this is a lookup and no fixup.
            line = "\tjp " name(lbl - 1)
        } else if (i % 8 == 6) {
            # Forward: the label does not exist yet, so this is a fixup that
            # is patched at the end.
            line = "\tcall " name(lbl + 1)
        } else {
            line = "\t" pool[(i % n) + 1]
        }
        print line
        size += length(line) + 1
        i++
    }
    # The last forward reference must have something to land on.
    print name(lbl + 1) ":"
}
function name(k,   a, b) {
    if (mode == "same") return sprintf("lbl_routine_body_%04d", k)

    # Two words with the counter between them. The counter is what makes it
    # unique -- deriving the whole name from a word list repeats, and the
    # reference refuses a label defined twice -- and it sits in the middle
    # rather than at the end so the last character still comes from a word.
    # Put the number last and every name ends in one of ten digits, which is
    # the case the pathological mode is for.
    a = word[(k % w) + 1]
    b = word[((k * 7 + 3) % w) + 1]
    if (k % 3 == 0) return a "_" k "_" b
    if (k % 3 == 1) return a "_" k "_" b "_end"
    return b "_" k "_" a "_loop"
}'
