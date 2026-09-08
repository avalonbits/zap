#!/bin/bash
# Generates a source containing every instruction form zap can assemble.
#
#   test/bench/gen_isa.sh even [bytes] > isa_even.s
#   test/bench/gen_isa.sh real [bytes] > isa_real.s
#   test/bench/gen_isa.sh degenerate [bytes] > isa_degenerate.s
#   test/bench/gen_isa.sh memory [bytes] > isa_memory.s
#   test/bench/gen_isa.sh include <dir> [bytes]     writes a tree of files
#
# gen_pure.sh cycles forty hand-picked forms, which is a plausible instruction
# stream but not a sample of the instruction set: it reaches 31 of the ISA's
# 114 mnemonics and 40 of its 322 rows, with no call, jp or djnz at all. A
# change that helps the shapes it happens to contain and hurts the ones it does
# not will still look like an improvement. That is not hypothetical -- it
# happened, and is written up in .internal/performance-notes.md.
#
# The forms come from test/cases/opcodes.s, which is the reference's own
# opcode corpus filtered to what *ez80asm* assembles -- see gen_opcodes.sh --
# plus the call forms that corpus happens not to contain. Deriving them from a
# file that is checked against ez80asm on every test run means these cannot
# drift into containing something zap gets wrong.
#
# That file used to be filtered through zap instead, which meant these
# benchmarks measured only the forms zap already handled: 53 it did not were
# absent, negative literals among them, so the branch that negates a value was
# never once executed by either distribution. Regenerating after that was fixed
# changed both files, and every timing taken before it is against different
# input and not comparable.
#
# TWO DISTRIBUTIONS, because neither alone answers the question:
#
#   even  Every form the same number of times. Says what the instruction set
#         costs. Nothing is over-weighted, so a change cannot look good by
#         helping whatever happens to be common here.
#
#   degenerate  Every label used before any is defined, and defined in the
#         reverse of the order they were used: the first half of the file
#         refers to L1..LN five times over, the second half defines LN..L1.
#         Nothing about it is realistic, which is the point -- it is the worst
#         case the one-pass design has.
#
#         Every reference is forward, so every one becomes a fixup and none can
#         be resolved where it is read; the fixup list reaches its largest and
#         stays there until the source runs out. Reversing the definitions
#         maximises the distance between a use and its definition: L1 is
#         referenced first and defined last.
#
#         What it is for is the shape of the cost, not the size of it. If
#         patching ever stops being linear in the number of fixups, this is
#         where it shows first.
#
#   memory  As many distinct labels as a source of that size can hold, which
#         is a different worst case from `degenerate` and was found by
#         measuring: that file maximises the fixup list and uses *less* memory
#         than isa_real, because it has 468 distinct labels against 1,941 and
#         the name arena and symbol blocks scale with the count.
#
#         A symbol costs eleven bytes of node plus its name, against the name
#         plus two bytes of source, so **short names are the expensive ones**:
#         2.8 bytes of table per byte of source at three characters, 1.4 at
#         twenty. Alternating short definitions with instructions is about the
#         worst a valid program can be.
#
#   include  A tree of ten source files and three blobs, written into a
#         directory rather than a stream. The root includes two, each of those
#         includes one or two, four levels deep -- a *tree* and not a chain,
#         because a chain only ever pushes readers and then pops them all,
#         while a tree pops back to a parent that still has lines left and
#         pushes again from there. That is the case that catches a parent
#         reader resumed at the wrong offset, and a chain cannot reach it.
#
#         Its figure is not comparable with the others, and the reason is not
#         the assembler: opening thirteen files on an emulated SD card is real
#         work no single-file source does, and it lands inside the same
#         "Done in" line. It measures the shape, not the throughput.
#
#   real  Weighted by how often each mnemonic appears in the two real programs
#         in test/corpus -- BBC BASIC and Rokky, 10,440 instructions between
#         them -- while still containing every form at least once. Says what
#         the assembler costs on the kind of source people actually feed it.
#
# LABELS ARE INCLUDED, one definition and one reference every eight lines,
# which is the rate counted over the two real programs: 11-15% of lines define
# a label and 38-62% of instruction lines refer to one. Half the references
# point backwards, so they resolve where they are read, and half forwards, so
# they go through the fixup list -- a file of only one kind would price
# whichever it happened to contain.
#
# The names vary in first character, last character and length. That mattered
# enormously to the key labels used to have and much less to the Pearson hash
# that replaced it, but a benchmark whose names are all one stem is measuring
# a naming convention rather than an assembler either way.
#
# They are also the *length* the corpus would have given them, which they were
# not until the fourth revision of this file. See lname.
#
# The instruction forms are interleaved rather than rewritten. Replacing the
# operand of a `jp` with a label would have kept the line count and lost the
# form: these files exist to contain every form, and an operand is part of one.
#
# NOT INCLUDED: jr and djnz, though not for want of correctness -- zap now
# assembles both byte-identically to the reference. A relative displacement
# reaches 127 bytes forward and 128 back, and without labels there is no way to
# write a target that stays in reach as the output grows past that in the first
# hundred bytes of a 256 KiB file. They are covered by
# test/cases/relative.s instead, which stays short for the same reason.
#
# They are 10.3% of real instructions, so the `real` weighting is optimistic by
# about that much.
#
# Deterministic: no randomness, no dependence on the environment. Changing this
# script invalidates every timing taken with it, and it has now changed six
# times: labels, then local labels, then anonymous ones, then the length of a
# label name, then macros, conditional assembly and ASSUME, then instruction
# mode suffixes. isa_real has gone from 19,399 lines to 22,068 to 22,458 to
# 23,749 to 21,806 to 21,494 across them.
# Nothing measured against an earlier version of this file is comparable with
# anything measured against this one -- the baselines below are the ones that
# count.
#
#   isa_real         5.44s   383 cycles/byte   21,494 lines
#   isa_even         5.88s   413               21,719
#   isa_degenerate   5.42s   381               22,530
#   isa_memory       5.86s   412               28,040
#
# Those four are up on the 5.28 / 5.34 / 4.86 / 5.32 recorded before, and none
# of it is this file: it has not changed. Two rounds of correctness work moved
# them -- deferred expressions, a deferred BLK fill, a displacement that is an
# expression, and a global minus a local settled at the end of its scope -- and
# then bounding every character scan added 0.06 to 0.08 across the set, and
# widening the evaluator to 32 bits another 0.08 to 0.10, and honouring .CPU
# another 0.02 to 0.08. What those last three bought and why none of them is
# optional is in .internal/performance-notes.md.
#
# Neither shows in these files directly. There is no DW32 and no BLKL in any
# of them, so what the width costs here is what it costs a source that never
# uses it -- which is the number that matters, and the reason it was worth
# three rounds of getting down from 7.4% to 1.8%.
#
# isa_degenerate and isa_memory build their own bodies and take none of this,
# so their figures move only when the assembler does -- which it has, hence 385
# to 374 on the second of them.
#
# Two of the earlier changes are worth understanding before reading those
# numbers, because both moved them in the direction nobody expects.
#
# Shortening the names made the per-byte figures **worse** -- isa_real from 332
# to 342 -- and the per-line figures better, 210 to 205 microseconds a line.
# Both are true and neither is a regression: the file is sized in bytes, so
# 5.8% shorter label text means 5.8% more lines inside the same 256 KiB, and a
# line costs more than the characters of a name do.
#
# The fifth change did the same thing in reverse and then some. A macro
# invocation is nine characters of source that expand into two or three
# assembled lines; both it and a conditional block buy more work per byte than
# the instruction they displace. The line count fell 8% and the cycles per byte
# rose from 356 to 384.
#
# Where that 28 went, measured by generating the same file with one feature
# left out at a time:
#
#   conditional assembly   free, inside the resolution of the measurement
#   ASSUME                 the same
#   macros                 0.46s of 5.46, all of it
#
# Which is not the feature being slow so much as the feature being work: an
# invocation assembles two more lines than the nine characters it occupies
# would otherwise have bought. It did start out slow -- 6.48s when the macros
# first went in, against 5.46 now -- and what came out of it is written up in
# .internal/performance-notes.md.
#
# The three of them together are why isa_real no longer reads under 350. The
# same file with the macros taken out reads 353. That is the honest position:
# the target was set against a file that did not exercise these paths, and a
# benchmark that leaves an expensive feature out to keep its number down is
# measuring the wrong thing.
#
# A round of work on the directive path since then took 384 to 368 without
# touching the file, so the gap is 18 rather than 34. isa_degenerate and
# isa_memory did not move at all, which is the check that it was the directive
# path: neither of them holds a directive.
#
# The mode suffixes then put it to 371. That is the whole price of 797 of them
# -- three cycles a byte, or about 925 cycles an instruction over what the
# unsuffixed form costs, which is the detour through the directive path and the
# second mnemonic lookup. The suffix is the densest thing the corpus has of
# everything added since the directives, at one line in 82, and it is the
# cheapest of them to assemble.
#
# What isa_real now holds, per 21,806 lines:
#
#   global      422 definitions,    843 references
#   local     1,264 definitions,    843 references
#   anonymous   211 definitions,    632 references
#   directives          3,181 lines, 14.6%
#   EQU                   421 lines,  1.9%
#   macros       12 definitions,    426 invocations
#   IF/ELSE/ENDIF       945 lines, 420 blocks, 105 with two arms
#   ASSUME              105 lines,  52 mode switches and back
#   mode suffixes       797 instructions, all eight spellings
#
# 21.6% of its lines define or name a label and another 16.5% are a directive
# or an EQU. Denser in both than real code, and meant to be, for the reason
# that keeps coming up: a benchmark with almost none of a thing in it cannot
# track what that thing costs. The corpus programs with realistic proportions
# are in test/corpus.
#
# Against the rates the corpus has, over its 186,050 lines excluding z88dk:
#
#                  here            corpus
#   IF          1 per   52     1 per   196
#   ASSUME      1 per  208     1 per   707
#   MACRO       1 per 1817     1 per 1,249
#   a suffix    1 per   26     1 per    82
#
# The first two are three to four times denser, which is the same multiple the
# labels and the other directives already carry. MACRO is the exception and is
# left at roughly the corpus rate on purpose: every definition also lengthens
# the list that each invocation walks, so inflating the count would price a
# lookup no real program performs. The invocations are dense instead, one every
# 51 lines, and the corpus offers no rate to copy for those -- it counts where
# macros are written, not where they are used.
#
# The values those EQUs take, against the 9,772 in the corpus:
#
#   hexadecimal   69.1%  (65.0)      decimal       8.8%  (10.1)
#   an expression 18.5%  (22.1)      binary        2.6%   (2.7)
#   a character    0.9%   (0.1)
#
# The bands are exact and the sample is not: 453 EQUs is four and a half turns
# of a hundred-slot cycle, and a partial turn skews it by a few points. The
# character literal is the one deliberate over-weighting, for coverage.
set -euo pipefail

cd "$(dirname "$0")/../.."

MODE="${1:?usage: gen_isa.sh <even|real|degenerate|memory|include> [bytes]}"

# Features left out, space separated, for attribution.
#
#   ISA_OMIT="macro cond" test/bench/gen_isa.sh real > without_macros.s
#
# The file is still the byte budget it was asked for, so leaving a feature out
# fills the space with ordinary instructions instead. That is what makes the
# difference between two of these the cost of the feature rather than the cost
# of a shorter file, and it is the same shape gen_comments.sh uses.
#
# Offered: equ, macro, cond, assume, suffix, data, macrocall.
#
# `macrocall` is the odd one and is what prices the macro *machinery*.
# Everything stays -- the definitions, the same number of scopes, the same
# lines assembled -- and each invocation is written out as the lines it would
# have expanded to. So the two files assemble the same program and one of them
# reaches it through macro_text, macro_run and a nested reader while the other
# does not.
#
# It has to be used with ISA_LINES, not with a byte budget. An expansion is
# thirty-odd characters where the invocation was nine, so under a byte budget
# the file with the expansions written out would hold about five per cent
# fewer scopes -- and five per cent of isa_real is twice the size of the thing
# being measured. Fixing the line count instead makes the two files identical
# except for the one slot.
#
#   ISA_LINES=21000 test/bench/gen_isa.sh real > with.s
#   ISA_LINES=21000 ISA_OMIT=macrocall test/bench/gen_isa.sh real > without.s
#
# NOT offered: labels. Every reference, every DL and DW operand, every scope
# boundary and the whole of finish() are built on them, so a file without them
# is not this file with one thing removed -- it is a different generator. What
# labels cost is in the note at the top of this file, measured the once by
# taking them out by hand and never repeated because the answer moved the file
# rather than the assembler.
ISA_OMIT="${ISA_OMIT:-}"

# Stop after this many instruction lines rather than after a byte budget.
#
# `real` only. The point is a pair of files with the same structure and a
# different size, which is the opposite of what the byte budget gives and is
# what comparing two ways of writing the same program needs.
ISA_LINES="${ISA_LINES:-0}"

# `include` writes a directory rather than a stream, because ten files cannot
# come out of one pipe. Everything else takes the byte budget as $2.
INCDIR=""
if [ "$MODE" = include ]; then
    INCDIR="${2:?usage: gen_isa.sh include <dir> [bytes]}"
    TOTAL="${3:-262144}"
    mkdir -p "$INCDIR"
else
    TOTAL="${2:-262144}"
fi
CASES="test/cases/opcodes.s"

if [ ! -f "$CASES" ]; then
    echo "missing $CASES" >&2
    exit 2
fi

# Measured over test/corpus/Z_PRG_Agon-bbc-basic-v and Z_PRG_Agon-Rokky,
# including their .inc files: 10,440 instructions, 52 distinct mnemonics.
# Anything absent from this list still appears, at weight 1.
WEIGHTS="ld:2378 call:1309 pop:734 push:711 ret:558 cp:537 inc:510 exx:429
jp:360 ex:288 or:245 add:229 dec:196 xor:150 sbc:116 bit:91 and:86 sub:40
adc:35 set:34 scf:28 res:26 rr:24 ldir:23 rl:21 ccf:21 cpir:15 rlca:14
cpl:14 rla:13 rrca:12 rra:10 neg:10 di:10 ei:9 sla:5 mlt:4 daa:4 srl:3
out0:3 lea:3 lddr:3 in0:3 sra:2 ldi:2 in:2 rrd:1 rld:1 reti:1 out:1"

{
    grep -v '^;' "$CASES" | grep -v '^[[:space:]]*$'
    for cc in "" "nz, " "z, " "nc, " "c, " "po, " "pe, " "p, " "m, "; do
        printf '  call %s0x040000\n' "$cc"
    done
} | sed 's/[[:space:]]*$//' | sort -u | awk -v mode="$MODE" -v total="$TOTAL" -v w="$WEIGHTS" -v incdir="$INCDIR" -v omit="$ISA_OMIT" -v lines="$ISA_LINES" '
BEGIN {
    # Label names, sized like the corpus rather than like a generator.
    #
    # Every entry is a length, and the 200 of them are the length distribution
    # of the 20,865 global labels defined across the Agon corpus, rounded to
    # 200 slots and shuffled once so that consecutive labels vary. Mean 8.39
    # against the corpus mean of 8.45, median 7 against 7, and the same spread: 10%
    # at four characters or fewer, 10% at fourteen or more.
    #
    # The corpus here is ~/agon-corpus **without z88dk**, which is a Z80 C
    # library rather than an Agon program and supplies 39,922 of the 60,787
    # labels in the tree. Its names are C symbols put through a mangler --
    # `cm48_sdccixp_ulonglong2ds_callee` -- and including it moves the mean
    # from 8.5 to 11.5 on the strength of one vendored dependency.
    nll = split("6 6 14 6 18 12 19 15 8 3 6 7 9 24 16 9 5 11 9 13 " \
                "6 6 9 11 4 5 16 9 5 7 5 3 6 7 9 2 5 6 4 6 " \
                "5 7 12 8 6 10 5 12 2 6 15 6 6 4 4 8 6 18 17 4 " \
                "3 8 10 7 16 2 6 6 6 9 5 6 9 6 15 21 12 5 7 13 " \
                "9 6 4 2 5 5 6 2 6 5 6 3 4 8 10 11 4 6 14 6 " \
                "6 8 14 11 6 5 9 9 6 10 13 10 6 7 4 13 7 4 16 6 " \
                "10 3 6 10 6 6 7 13 5 6 6 8 10 9 12 14 5 8 5 9 " \
                "11 11 6 6 5 7 6 12 6 8 8 14 9 11 19 10 17 12 15 7 " \
                "6 10 13 7 10 4 11 4 10 17 7 6 15 14 12 20 14 7 7 7 " \
                "8 11 2 9 10 5 6 6 7 9 6 6 4 13 5 8 9 7 3 13",
                llen, " ")
    # Where each entry sits among the others of its own length, so that a name
    # can be built from its index alone without the generator having to have
    # emitted the ones before it.
    for (i = 1; i <= nll; i++) {
        lrank[i] = lcnt[llen[i]] + 0
        lcnt[llen[i]]++
    }

    # Words to cut name fragments out of, joined by underscores and then
    # doubled so that a fragment starting anywhere in the first copy can run to
    # any length without falling off the end. Fragments always begin at a word,
    # so a name reads as one -- `draw_move_sca` rather than a slice of the
    # middle of something.
    nlw = split("read|write|draw|move|scan|calc|emit|parse|check|reset|" \
                "flush|store|load|swap|clip|tile|sprite|sound|timer|port|" \
                "queue|stack|frame|pixel|glyph|board|piece|score|level|input",
                lw, "|")
    lpool = ""
    for (i = 1; i <= nlw; i++) {
        lstart[i] = length(lpool)
        lpool = lpool lw[i] "_"
    }
    lpool = lpool lpool
    # Nineteen letters with no a..f and no h among them, so a name made only of
    # these cannot also be read as a hexadecimal literal with a trailing h.
    # That ambiguity is real -- see mname below, which met it first.
    lalpha = "gijklmnopqrstuvwxyz"
    nalpha = length(lalpha)
    nw = split(w, wa, /[ \n]+/)
    for (i = 1; i <= nw; i++) {
        if (wa[i] == "") continue
        split(wa[i], kv, ":")
        weight[kv[1]] = kv[2]
    }
}
{ form[n++] = $0 }
END {
    if (n == 0) { print "no forms" > "/dev/stderr"; exit 1 }

    if (mode == "memory") {
        # One line in three is a definition, with the shortest name that stays
        # unique and unambiguous: a letter and three base-36 digits, which is
        # 1.2 million names and cannot be mistaken for a mnemonic.
        #
        # One in three and not one in two, because **one in two does not fit**.
        # At that density 256 KB of source is 15,460 labels and zap runs out
        # of memory on the Agon at line 28,673 of 30,920 -- 93% of the way
        # through -- needing about 313 KB of heap against roughly 310 KB that
        # a 512 KB machine has left after MOS and the program. That is the real
        # ceiling and it is worth knowing; it is not a benchmark, because a
        # benchmark that fails measures nothing and tracks no regression.
        #
        # One reference every sixteenth line, so the fixup list is exercised
        # without dominating -- what this file is for is the table behind the
        # labels, not the list of unresolved uses.
        bytes = 0
        i = 0
        k = 0
        while (bytes < total) {
            if (i % 3 == 0) {
                line = mname(k) ":"
                k++
            } else if (i % 16 == 7 && k > 1) {
                line = "  jp " mname(int(k / 2))
            } else {
                line = form[i % n]
            }
            print line
            bytes += length(line) + 1
            i++
        }
        exit 0
    }

    if (mode == "degenerate") {
        # Sized so that the references fill the first half and the definitions
        # fit in the second. One reference every fourth line, five uses each.
        half = int(total / 2)
        refs = int(half / 56)          # ~56 bytes of source per reference
        nl = int(refs / 5)
        if (nl < 1) nl = 1

        bytes = 0
        i = 0
        r = 0
        while (bytes < half) {
            if (r < nl * 5 && i % 4 == 3) {
                # L1, L2, ... LN, and round again: five passes in total.
                line = "  call " lname((r % nl) + 1)
                r++
            } else {
                line = form[i % n]
            }
            print line
            bytes += length(line) + 1
            i++
        }

        # Top up to five uses each: the byte budget can end the loop above
        # early, and "at least five" is the point of the shape.
        while (r < nl * 5) {
            line = "  call " lname((r % nl) + 1)
            print line
            bytes += length(line) + 1
            r++
        }

        # LN down to L1, so the label used first is defined last.
        d = nl
        while (bytes < total || d > 0) {
            if (d > 0 && i % 4 == 3) {
                line = lname(d) ":"
                d--
            } else {
                line = form[i % n]
            }
            print line
            bytes += length(line) + 1
            i++
        }
        exit 0
    }

    if (mode == "include") {
        include_tree()
        exit 0
    }

    if (mode == "even") {
        # Strided rather than sequential. The forms arrive grouped by mnemonic,
        # and emitting them in that order would put every `ld` together, so a
        # run of the file would measure one mnemonic at a time rather than the
        # mix. A stride coprime with the count visits every form exactly once
        # per pass while separating neighbours.
        stride = int(n / 2) + 1
        while (stride > 1 && gcd(stride, n) != 1) stride--
        if (stride < 1) stride = 1
        i = 0
        bytes = org_header()
        while (bytes < total) {
            bytes += out(form[i % n])
            i += stride
        }
        finish()
        exit 0
    }

    # real: every form once, then weighted by mnemonic.
    bytes = org_header()
    for (i = 0; i < n && bytes < total; i++) {
        bytes += out(form[i])
    }

    # Buckets of forms per mnemonic, and a cumulative weight table over the
    # mnemonics that actually have forms.
    for (i = 0; i < n; i++) {
        m = form[i]
        sub(/^[[:space:]]+/, "", m)
        sub(/[[:space:]].*$/, "", m)
        m = tolower(m)
        bucket[m, cnt[m]++] = form[i]
        if (!(m in seen)) { seen[m] = 1; mn[k++] = m }
    }
    cum = 0
    for (j = 0; j < k; j++) {
        ww = (mn[j] in weight) ? weight[mn[j]] : 1
        cum += ww
        edge[j] = cum
    }

    # Walks the cumulative table with a fixed step, so the sequence is fixed
    # and reproducible but does not repeat a short cycle.
    step = 7919
    pos = 0
    while (lines > 0 ? ln < lines : bytes < total) {
        pos = (pos + step) % cum
        lo = 0; hi = k - 1
        while (lo < hi) { mid = int((lo + hi) / 2); if (pos < edge[mid]) hi = mid; else lo = mid + 1 }
        m = mn[lo]
        bytes += out(bucket[m, use[m]++ % cnt[m]])
    }
    finish()
}

# Prints one instruction, and the label lines that go with it. Returns the
# bytes printed, so both loops keep their budget.
#
# The cycle is 32 lines, which is one scope. It holds four definitions and four
# references -- the same density as the eight-line cycle it replaces, so the
# label count per byte has not moved -- but three of the four definitions are
# local and two of the four references are, one forward and one backward of
# each kind.
#
#     0   global definition          opens the scope
#     4   backward global reference
#     8   local definition   @loop
#    12   backward local reference   @loop, defined above in this scope
#    16   local definition   @done
#    20   forward local reference    @skip, defined below in this scope
#    24   local definition   @skip
#    28   forward global reference   the label opening the next scope
#
# Every local reference stays inside its scope because a local cannot reach out
# of one. Three locals per scope is above the corpus median of two and well
# under its worst of twenty. (No apostrophes in these comments: the whole
# program is inside a single-quoted shell string.)
#
# Every other scope carries anonymous labels as well, which are not scope-bound
# and so need no such care:
#
#     2   forward anonymous reference    @f
#     3   forward anonymous reference    @f, sharing the pending symbol
#     6   anonymous definition           @@, which resolves both of them
#    10   backward anonymous reference   @b, the one at 6
#
# One definition and three references per 64 lines, against six local
# definitions and four local references over the same span. The corpus has 171
# anonymous definitions against roughly 800 local ones, so this is heavier on
# them than real code is -- deliberately, because a benchmark that contains
# almost none of a thing cannot track what it costs.
# A tree of included files, not a chain.
#
# The root includes two, each of those includes one or two, and so on: nine
# files below the root, four levels deep. A chain would only ever push readers
# and then pop them all; a tree pushes, pops back to a parent that still has
# lines left, and pushes again from there -- which is the case that catches a
# parent resumed at the wrong offset, and the one a straight chain cannot
# reach.
#
#                       isa_include.s
#                        /         \
#                    inc_a       inc_b
#                    /   \       /   \
#                inc_c  inc_d  inc_e  inc_f
#                  |      |      |
#                inc_g  inc_h  inc_i
#
# Binary blobs are pulled in with INCBIN at three points, which need no reader
# at all and so exercise the other half of the pair.
#
# Every file is a flat name in one directory, because the reference resolves an
# include relative to where the assembler runs and not to the file doing the
# including -- measured, and it means the same paths work on the host and on
# the Agon, where the sdcard root is the working directory.
function include_tree(   i, share, want, k) {
    nfile = split("isa_include.s inc_a.inc inc_b.inc inc_c.inc inc_d.inc" \
                  " inc_e.inc inc_f.inc inc_g.inc inc_h.inc inc_i.inc",
                  fname, " ")

    # Who includes whom, as a space-separated list per file.
    kidsof[1] = "2 3"          # root -> a, b
    kidsof[2] = "4 5"          # a    -> c, d
    kidsof[3] = "6 7"          # b    -> e, f
    kidsof[4] = "8"            # c    -> g
    kidsof[5] = "9"            # d    -> h
    kidsof[6] = "10"           # e    -> i
    kidsof[7] = ""
    kidsof[8] = ""
    kidsof[9] = ""
    kidsof[10] = ""

    # Where each file pulls in a blob. The root takes one, a middle file takes
    # one and a leaf takes one, so INCBIN appears at three different depths.
    binof[1] = "blob1.bin"
    binof[5] = "blob2.bin"
    binof[10] = "blob3.bin"
    write_blob(incdir "/blob1.bin", 64)
    write_blob(incdir "/blob2.bin", 300)
    write_blob(incdir "/blob3.bin", 17)

    # The budget is split unevenly on purpose: a root that is mostly includes
    # and leaves that hold most of the text is what a real program looks like.
    weightof[1] = 6;  weightof[2] = 8;  weightof[3] = 8;  weightof[4] = 12
    weightof[5] = 12; weightof[6] = 12; weightof[7] = 14; weightof[8] = 14
    weightof[9] = 14; weightof[10] = 14
    share = 0
    for (i = 1; i <= nfile; i++) {
        share += weightof[i]
    }

    # Headers first, for every file: the comment, the ORG in the root, the
    # includes and any blob. Written before any body so that the bodies can go
    # in a different order below, appending to files that already exist.
    for (i = 1; i <= nfile; i++) {
        write_header(i)
    }

    # And now the bodies, in the order the assembler will read them.
    #
    # This is the part that has to be right. A file lists its includes at the
    # top, so the reader descends before it reads a line of the body: the
    # stream is a depth-first walk in which a parent body comes *after* all of
    # its children. The label cycle, the scopes and the forward references all
    # run continuously through that stream, so generating the bodies in file
    # order would number them in an order the assembler never sees -- and the
    # locals stop matching their scopes. It reported as
    # `inc_g.inc line 2: unknown label`.
    nbody = 0
    dfs_bodies(1)
    for (k = 1; k <= nbody; k++) {
        i = bodyof[k]
        want = int(total * weightof[i] / share)
        write_body(i, want)
    }

    # The last body in that order is the root, so this is the end of the
    # stream, which is where the outstanding forward references have to land.
    outfile = incdir "/" fname[1]
    finish()
    for (i = 1; i <= nfile; i++) {
        close(incdir "/" fname[i])
    }
    outfile = ""
}

# The order the bodies are read in: every child of a file, and then the file.
function dfs_bodies(i,   kn, kf, j) {
    kn = split(kidsof[i], kf, " ")
    for (j = 1; j <= kn; j++) {
        dfs_bodies(kf[j] + 0)
    }
    bodyof[++nbody] = i
}

function write_header(i,   kn, kf, j, t) {
    outfile = incdir "/" fname[i]
    emit("; " fname[i] " -- generated by gen_isa.sh include")
    used[i] = length(fname[i]) + 36

    if (i == 1) {
        # The ORG, the ASSUME and the macro definitions, ahead of the INCLUDE
        # lines below: the children are assembled where their INCLUDE sits, so
        # a macro they invoke has to be defined before it.
        used[i] += org_header()
    }

    kn = split(kidsof[i], kf, " ")
    for (j = 1; j <= kn; j++) {
        t = "  INCLUDE \"" fname[kf[j] + 0] "\""
        emit(t)
        used[i] += length(t) + 1
    }
    if (i in binof) {
        t = "  INCBIN \"" binof[i] "\""
        emit(t)
        used[i] += length(t) + 1
    }
    outfile = ""
}

# A file body stops on a scope boundary, not on the byte budget alone.
#
# The reference scopes local labels **per file**: a local defined in one and
# named in another is "Unknown identifier" there, in either direction. zap is
# more permissive and lets a scope cross an include, so a file cut at an
# arbitrary line assembles here and not there -- and these files exist to be
# compared. Ending on a multiple of 32 keeps every local, and every anonymous
# reference, inside the file that opened it.
function write_body(i, want) {
    outfile = incdir "/" fname[i]
    while (used[i] < want || ln % 32 != 0) {
        used[i] += out(form[fi++ % n])
    }
    outfile = ""
}

# A blob for INCBIN. Bytes, not text: the point is that nothing reads it as
# source.
#
# Values stay under 128 because awk writes %c through the locale, and anything
# above that comes out as two UTF-8 bytes -- which makes the file a different
# size from the one asked for. What the bytes are does not matter; how many
# there are does.
function write_blob(path, n,   i) {
    printf "" > path
    for (i = 0; i < n; i++) {
        printf "%c", (i * 37 + 11) % 128 > path
    }
    close(path)
}

# The value on an EQU line, in the proportions the corpus has. See the comment
# at its call site for where the numbers come from.
function equ_value(k,   band) {
    band = k % 100
    if (band < 65) {
        return sprintf("0x%04X", (k * 37) % 65536)
    }
    if (band < 75) {
        return (k * 7) % 1000
    }
    if (band < 78) {
        return sprintf("%%%s", substr("10110100", 1 + (k % 4), 4))
    }
    if (band < 79) {
        # 39 is the apostrophe, which cannot be written here: the whole program
        # is inside a single-quoted shell string.
        return sprintf("%c%s%c", 39, substr("ABCDEFGHIJKLMNOP", 1 + (k % 16), 1), 39)
    }

    return "[" (k % 97) " + 3] * 2 - " (k % 7)
}

# One relocating ORG at the top, which moves the origin and writes nothing.
#
# The padding form appears later, once every 64 scopes; this is the other arm
# of the same directive and it is the one every real program has. It costs
# nothing to run and is here so that neither arm is absent.
#
# Then the ASSUME every Agon program opens with, and the macros the body
# invokes. Both go through emit(), because the include tree needs them in the
# root header ahead of its INCLUDE lines: the children are assembled before the
# root body is, so a macro defined down there would not exist when they run.
function org_header(   t, i, nh, h, used) {
    t = "  ORG 0x040000"
    emit(t)
    used = length(t) + 1

    # Not redundant with the default -- both assemblers already start in ADL
    # mode -- but every real program states it, and the line has to be paid for
    # somewhere. The mode switch itself appears in the body; see the island.
    t = "  ASSUME ADL = 1"
    emit(t)
    used += length(t) + 1

    # Six macros, sized like the corpus: over the 149 definitions in it the
    # parameter counts run 0:57, 1:61, 2:20, 3:9, 4:2 and the bodies are one to
    # three lines far more often than anything longer. These are 0, 0, 1, 2, 3
    # and 0 parameters, with bodies of two, two, one, two, three and three.
    #
    # `mwait` holds a local label, which is the case worth having: an expansion
    # gets a scope of its own, so the same body invoked in a hundred scopes
    # defines `@spin` a hundred times without a redefinition, and none of them
    # is visible to the caller.
    # Semicolons between the macros, a bar between a header and its body and a
    # slash between body lines: the headers and the bodies both contain spaces,
    # so the list cannot be split on one.
    nh = split("msave|push af/push bc" \
               ";mrest|pop bc/pop af" \
               ";mload v|ld a, v" \
               ";msum v, w|ld a, v/add a, w" \
               ";mtri v, w, x|ld a, v/add a, w/sub x" \
               ";mwait|@spin:/dec bc/jp @spin", h, ";")
    for (i = 1; i <= nh; i++) {
        used += macro_def(h[i])
    }

    return used
}

# One macro, written from a "name params|line/line" spec. Returns its bytes.
function macro_def(spec,   head, body, nb, b, j, t, used) {
    head = spec
    body = spec
    sub(/\|.*$/, "", head)
    sub(/^[^|]*\|/, "", body)
    t = "  MACRO " head
    emit(t)
    used = length(t) + 1
    nb = split(body, b, "/")
    for (j = 1; j <= nb; j++) {
        t = (substr(b[j], length(b[j]), 1) == ":") ? b[j] : "  " b[j]
        emit(t)
        used += length(t) + 1
    }
    emit("  ENDMACRO")
    used += 11

    return used
}

# `outfile` is empty for the modes that write one stream to stdout, and names a
# file for the include tree. Every print below goes through emit() so that the
# two cases share one body.
# One macro body, written out with the arguments of this scope substituted.
#
# Kept beside the invocations it stands in for, and the two must be edited
# together: the whole of ISA_OMIT=macrocall is that the lines here are the
# lines the expansion would have produced.
function body(m,   t, used) {
    used = 0
    if (m == 0) {
        used += say("  push af") + say("  push bc")
    } else if (m == 1) {
        used += say("  ld a, " (lbl % 251))
    } else if (m == 2) {
        used += say("  ld a, " (lbl % 97)) + say("  add a, " (lbl % 31))
    } else if (m == 3) {
        used += say("@spin:") + say("  dec bc") + say("  jp @spin")
    } else if (m == 4) {
        used += say("  ld a, " (lbl % 61)) + say("  add a, " (lbl % 29)) \
                + say("  sub " (lbl % 13))
    } else {
        used += say("  pop bc") + say("  pop af")
    }

    return used
}

# Emits a line and returns the bytes it took, so a caller can add them up.
function say(t) {
    emit(t)

    return length(t) + 1
}

# Whether a feature was left out. See ISA_OMIT above.
function off(f) {
    return index(" " omit " ", " " f " ") > 0
}

function emit(t) {
    if (outfile == "") {
        print t
    } else {
        print t > outfile
    }
}

function out(line,   used, k, t, m) {
    used = 0
    k = ln % 32
    if (k == 0) {
        lbl++
        used += length(lname(lbl)) + 2
        emit(lname(lbl) ":")
    } else if (k == 1 && !off("equ")) {
        # A name for a value, at the top of the scope and nowhere else. An EQU
        # ends the enclosing scope exactly as a global label does -- measured
        # against the reference -- so one in the middle would put the locals
        # below it in a different scope from the references above it, and the
        # file would stop assembling.
        #
        # The value is drawn from the mix the corpus has, counted over its
        # 9,772 EQU definitions:
        #
        #     hexadecimal   65.0%      decimal       10.1%
        #     an expression 22.1%      binary         2.7%
        #     a character    0.1%
        #
        # Every one of these used to be an expression, on the reasoning that an
        # EQU which is only a number never reaches the evaluator. True, and it
        # made every EQU in the file four times harder than the average real
        # one -- the same fault as the label lengths two revisions ago, and as
        # the directives being absent one revision ago: a benchmark answering a
        # narrower question than the one being asked of it.
        #
        # The character literal is the one deliberate over-weighting, at one in
        # a hundred against the one in a thousand the corpus has, because at 447
        # EQUs a file the honest share is less than one, and a path with none
        # of a thing in it is not being measured at all.
        #
        # Square brackets and not parentheses in the expression: the reference
        # has no parentheses, and these files exist to be compared against it
        # byte for byte.
        t = "eq" lbl ": EQU " equ_value(lbl)
        used += length(t) + 1
        emit(t)
    } else if (k == 5 && !off("macro")) {
        # A macro invocation, once a scope. The corpus counts definitions and
        # not invocations, so there is no rate to copy here; one per 32 lines
        # is the same reasoning as everything else in this cycle -- dense
        # enough that the path shows up in a measurement.
        #
        # Six of them in rotation, so no single body and no single parameter
        # count stands for the feature, and so that the walk down the macro
        # list reaches the far end of it as often as the near end.
        m = lbl % 6
        if (off("macrocall")) {
            # The same lines, written out. The bodies are the ones the header
            # defines and the arguments are the ones the invocation passes, so
            # what is assembled is what the expansion would have been -- and
            # `@spin` lands in the caller scope rather than in a scope of its
            # own, which is where an expansion would have put it. Nothing else
            # in the scope is called that.
            used += body(m)
        } else {
            if (m == 0)      t = "  msave"
            else if (m == 1) t = "  mload " (lbl % 251)
            else if (m == 2) t = "  msum " (lbl % 97) ", " (lbl % 31)
            else if (m == 3) t = "  mwait"
            else if (m == 4) t = "  mtri " (lbl % 61) ", " (lbl % 29) ", " (lbl % 13)
            else             t = "  mrest"
            used += length(t) + 1
            emit(t)
        }
    } else if (k == 9 && !off("cond")) {
        # A conditional block, lines 9 to 13 of the scope. Nothing inside it
        # defines a label: an unassembled definition would leave whatever
        # referred to it unresolved, and the condition is false half the time
        # by construction.
        #
        # `eq` was defined at line 1 of this scope, so the condition is a
        # symbol and an expression rather than a literal -- which is what real
        # code puts there, and it means the value has to be evaluated before
        # the block can be skipped or kept.
        # The condition names the EQU above unless there is not one, in
        # which case it is a literal -- so `cond` can be priced with `equ`
        # left out and the other way round.
        t = off("equ") ? ("  IF " (lbl % 2)) : ("  IF eq" lbl " & 1")
        used += length(t) + 1
        emit(t)
    } else if (k == 13 && !off("cond")) {
        used += 8
        emit("  ENDIF")
    } else if (k == 17 && lbl % 8 == 0 && !off("assume")) {
        # A mode switch and back, with two instructions inside that encode
        # differently on either side of it: `ld hl, nn` is three bytes in Z80
        # mode and four in ADL. The island is closed before this line is
        # printed, so nothing else in the file depends on where it fell.
        #
        # Twice per eight scopes is one ASSUME every 128 lines, against one
        # every 707 in the corpus. Denser on purpose, for the usual reason.
        emit("  ASSUME ADL = 0")
        emit("  ld hl, 0x1234")
        emit("  ld a, 5")
        emit("  ASSUME ADL = 1")
        used += 17 + 16 + 11 + 17
    } else if (k == 22 && !off("suffix")) {
        # An instruction carrying a mode suffix.
        #
        # The corpus has one every 82 lines, which is denser than anything else
        # here started out -- one IF every 196, one macro invocation every 198,
        # one ASSUME every 707. Agon programs call MOS, MOS is reached by
        # `RST.LIL $08`, and half of every suffix in the corpus is that one
        # line. Two slots a scope is about three times the corpus rate, which
        # is the band the labels and the directives already sit in.
        #
        # Weighted the way the corpus is: .lil is 82% of its uses, and the
        # rest are .sis, .lis, .l and .s. The two immediate widths are both
        # here because the suffix overrides the ADL mode rather than following
        # it, and that is the substantive half of the feature.
        m = lbl % 8
        if (m == 0)      t = "  rst.lil 8"
        else if (m == 1) t = "  ld.lil hl, 0x" sprintf("%06X", (lbl * 37) % 16777216)
        else if (m == 2) t = "  push.lil bc"
        else if (m == 3) t = "  ld.sis hl, 0x" sprintf("%04X", (lbl * 11) % 65536)
        else if (m == 4) t = "  inc.lil hl"
        else if (m == 5) t = "  pop.lil de"
        else if (m == 6) t = "  add.lil hl, bc"
        else             t = "  ret.lil"
        used += length(t) + 1
        emit(t)
    } else if (k == 26 && !off("suffix")) {
        # The second one, holding the shapes the first does not: the short
        # spellings, whose meaning depends on the mode they are read in, and a
        # label operand -- including one still ahead, so a fixup is patched at
        # a width the ADL mode would not have chosen.
        m = lbl % 6
        if (m == 0)      t = "  rst.lis 0x38"
        else if (m == 1) t = "  call.lil " lname(lbl)
        else if (m == 2) t = "  ld.l hl, 0x" sprintf("%06X", (lbl * 53) % 16777216)
        else if (m == 3) t = "  ld.sis hl, " lname(lbl + 1)
        else if (m == 4) t = "  jp.lil " lname(lbl)
        else             t = "  ld.s hl, 0x" sprintf("%04X", (lbl * 7) % 65536)
        used += length(t) + 1
        emit(t)
    } else if (k == 29 && lbl % 64 == 0 && !off("macro")) {
        # A macro defined mid-file and used once, which is the other half of
        # the feature: the six in the header are all captured before a line of
        # the body is read, and capture is a per-line cost of its own.
        #
        # One every 64 scopes is one definition every 2,048 lines, against one
        # every 1,249 in the corpus -- the one rate here that is *not* inflated,
        # because each definition also lengthens the list every invocation
        # walks, and that cost belongs at its real size.
        used += macro_def("mg" lbl " v|ld hl, v/ld (hl), 0")
        t = "  mg" lbl " 0x" sprintf("%04X", (lbl * 13) % 65536)
        used += length(t) + 1
        emit(t)
    } else if (k == 7 && !off("data")) {
        t = "  DB " (lbl % 251) ", " ((lbl * 3) % 251) ", 0x" \
            sprintf("%02X", (lbl * 7) % 256) ", -1"
        used += length(t) + 1
        emit(t)
    } else if (k == 11 && !(off("data") && off("cond"))) {
        # Every fourth scope splits the block in two here, which is close to
        # the share the corpus has: 35 ELSE against 951 IF, so most conditionals
        # have only the one arm. Whichever arm is not taken is skipped, and the
        # two arms cost different things, so both need to appear.
        # Two features share this slot, so each is gated on its own rather
        # than the branch as a whole -- an early return here would skip the
        # instruction the line is really about.
        if (lbl % 4 == 0 && !off("cond")) {
            used += 7
            emit("  ELSE")
        }
        # A word list with a name in it, so the directive takes a fixup at a
        # width an instruction never asks for.
        if (!off("data")) {
            t = "  DW 0x" sprintf("%04X", (lbl * 11) % 65536) \
                (off("equ") ? "" : (", eq" lbl))
            used += length(t) + 1
            emit(t)
        }
    } else if (k == 15 && !off("data")) {
        t = "  DB \"row " lbl " of the table\", 0"
        used += length(t) + 1
        emit(t)
    } else if (k == 19 && !off("data")) {
        # Reserved space and alignment alternate, so both the fill loop and the
        # distance-to-the-next-multiple appear.
        t = (lbl % 2 == 0) ? "  DS 4" : "  ALIGN 4"
        used += length(t) + 1
        emit(t)
    } else if (k == 27 && !off("data")) {
        # Three bytes of a label that has already been defined, which is the
        # width a fixup uses for an address.
        t = "  DL " lname(lbl)
        used += length(t) + 1
        emit(t)
    } else if (k == 31 && lbl % 64 == 0 && !off("data")) {
        # The padding ORG, rarely: it writes 0xFF into the output without
        # taking a byte of source, and once every 64 scopes is enough to keep
        # the path exercised without the output running away.
        t = "  ORG $ + 8"
        used += length(t) + 1
        emit(t)
    } else if (k == 8) {
        used += 7
        emit("@loop:")
    } else if (k == 16) {
        used += 7
        emit("@done:")
    } else if (k == 24) {
        used += 7
        emit("@skip:")
    } else if (k == 4) {
        if (lbl > 1) {
            used += length(lname(lbl - 1)) + 6
            emit("  jp " lname(lbl - 1))
        }
    } else if (k == 12) {
        used += 11
        emit("  jp @loop")
    } else if (k == 20) {
        used += 13
        emit("  call @skip")
    } else if (k == 28) {
        used += length(lname(lbl + 1)) + 8
        emit("  call " lname(lbl + 1))
    } else if (int(ln / 32) % 2 == 1) {
        # Anonymous labels, in every other scope. They are not scope-bound --
        # @f reaches the next @@ anywhere below and @b the last one anywhere
        # above -- so unlike the locals these need no care about where the
        # scope ends, only that one @@ follows the last @f in the file.
        if (k == 2) {
            used += 8
            emit("  jp @f")
        } else if (k == 3) {
            # A second forward reference before the same @@, because every @f
            # since the last one shares a single pending symbol and resolving
            # them together is the part worth exercising.
            used += 10
            emit("  call @f")
        } else if (k == 6) {
            used += 4
            emit("@@:")
        } else if (k == 10) {
            used += 8
            emit("  jp @b")
        }
    }
    ln++
    emit(line)
    return used + length(line) + 1
}

# The forward references still outstanding have to land on something.
#
# The locals first and the global after them: a global would end the scope, and
# a local defined on the far side of that is in the wrong one. Each is emitted
# only if this scope had not already reached the line that defines it, because
# a second definition in one scope is an error.
function finish(   k) {
    # A conditional block closed first, if the budget ran out inside one. The
    # IF goes in at line 9 and the ENDIF at 13, so a file that stopped anywhere
    # between them has an open block -- and every label below would be inside
    # it, which is how a truncated file stops assembling.
    k = ln % 32
    if (k >= 10 && k <= 13 && !off("cond")) {
        emit("  ENDIF")
    }

    # An anonymous one first, unconditionally: a forward reference to one is
    # cheap to leave outstanding and impossible to redefine, so emitting one
    # that nothing needs costs a line and emitting none where one is needed
    # fails the file.
    emit("@@:")
    k = ln % 32
    if (k > 0) {
        if (k <= 8)  emit("@loop:")
        if (k <= 16) emit("@done:")
        if (k <= 24) emit("@skip:")
    }
    emit(lname(lbl + 1) ":")
}

# A letter and three base-36 digits: short, unique, and a name rather than a
# literal.
#
# The leading letter is drawn from g..z, which is not a hexadecimal digit. With
# a..f allowed, `a00h` comes out -- and that is a hexadecimal literal with a
# trailing h, which the reference reads as 0xA00 and refuses as a label. It is
# the same ambiguity the operand parser resolves in favour of the number, and a
# generator that produces it is testing the ambiguity rather than the memory.
function mname(k,   d, g, r, i2, out2) {
    d = "0123456789abcdefghijklmnopqrstuvwxyz"
    g = "ghijklmnopqrstuvwxyz"
    r = int(k / 20)
    out2 = ""
    for (i2 = 0; i2 < 3; i2++) {
        out2 = substr(d, (r % 36) + 1, 1) out2
        r = int(r / 36)
    }

    return substr(g, (k % 20) + 1, 1) out2
}

# A label of the length the corpus would have given it, unique, and a name
# rather than a number.
#
# It used to be two words and the index -- `board_145_level_end` -- which
# averaged 17.1 characters against the corpus mean of 8.5 and never produced one
# shorter than eleven. Every label is hashed once and compared at least once,
# so the benchmark was doing 2x the per-label character work of real code, and
# anything it said about the symbol table was inflated by about that much. It
# had already bent two conclusions; see .internal/zap-to-zap.md.
#
# Length comes from the table above. Uniqueness comes from `s`, the rank a
# label holds among the ones of its own length, spelled in decimal and put at the end:
# the fragment in front of it is letters only, so where the digits begin is
# never in doubt and two different ranks cannot spell the same name.
#
# A length too short to hold both a fragment and a rank -- two characters, with
# a rank of ten or more -- is spelled in base nineteen instead. Those end in a
# letter and the others end in a digit, so the two cannot collide either.
function lname(k,   j, L, s, d, want, r, out) {
    j = (k - 1) % nll
    L = llen[j + 1]
    s = int((k - 1) / nll) * lcnt[L] + lrank[j + 1]
    d = s ""
    want = L - length(d)
    if (want >= 1) {
        # The stride matters. `j` alone walks the word list one step per
        # label, so neighbouring labels came out sharing long prefixes --
        # `parse_check_rese0` right after `calc_emit_pars0` -- and a shared
        # prefix is exactly what the name compare charges for. Real code does
        # that sometimes; a generator should not do it every time.
        r = (j * 13 + s * 7) % nlw
        return substr(lpool, lstart[r + 1] + 1, want) d
    }
    out = ""
    r = s
    while (length(out) < L) {
        out = substr(lalpha, (r % nalpha) + 1, 1) out
        r = int(r / nalpha)
    }

    return out
}
function gcd(a, b,  t) { while (b) { t = b; b = a % b; a = t } return a }
'
