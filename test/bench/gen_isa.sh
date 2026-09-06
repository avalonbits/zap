#!/bin/bash
# Generates a source containing every instruction form dzap can assemble.
#
#   test/bench/gen_isa.sh even [bytes] > isa_even.s
#   test/bench/gen_isa.sh real [bytes] > isa_real.s
#   test/bench/gen_isa.sh degenerate [bytes] > isa_degenerate.s
#   test/bench/gen_isa.sh memory [bytes] > isa_memory.s
#
# gen_pure.sh cycles forty hand-picked forms, which is a plausible instruction
# stream but not a sample of the instruction set: it reaches 31 of the ISA's
# 114 mnemonics and 40 of its 322 rows, with no call, jp or djnz at all. A
# change that helps the shapes it happens to contain and hurts the ones it does
# not will still look like an improvement. That is not hypothetical -- it
# happened, and is written up in .internal/performance-notes.md.
#
# The forms come from dzap/test/cases/opcodes.s, which is the reference's own
# opcode corpus filtered to what *ez80asm* assembles -- see gen_opcodes.sh --
# plus the call forms that corpus happens not to contain. Deriving them from a
# file that is checked against ez80asm on every test run means these cannot
# drift into containing something dzap gets wrong.
#
# That file used to be filtered through dzap instead, which meant these
# benchmarks measured only the forms dzap already handled: 53 it did not were
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
# The instruction forms are interleaved rather than rewritten. Replacing the
# operand of a `jp` with a label would have kept the line count and lost the
# form: these files exist to contain every form, and an operand is part of one.
#
# NOT INCLUDED: jr and djnz, though not for want of correctness -- dzap now
# assembles both byte-identically to the reference. A relative displacement
# reaches 127 bytes forward and 128 back, and without labels there is no way to
# write a target that stays in reach as the output grows past that in the first
# hundred bytes of a 256 KiB file. They are covered by
# dzap/test/cases/relative.s instead, which stays short for the same reason.
#
# They are 10.3% of real instructions, so the `real` weighting is optimistic by
# about that much.
#
# Deterministic: no randomness, no dependence on the environment. Changing this
# script invalidates every timing taken with it -- and adding labels did, so
# nothing measured before 2026-09-06 is comparable with anything measured
# after.
set -euo pipefail

cd "$(dirname "$0")/../.."

MODE="${1:?usage: gen_isa.sh <even|real|degenerate|memory> [bytes]}"
TOTAL="${2:-262144}"
CASES="dzap/test/cases/opcodes.s"

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
} | sed 's/[[:space:]]*$//' | sort -u | awk -v mode="$MODE" -v total="$TOTAL" -v w="$WEIGHTS" '
BEGIN {
    nlw = split("read|write|draw|move|scan|calc|emit|parse|check|reset|" \
                "flush|store|load|swap|clip|tile|sprite|sound|timer|port|" \
                "queue|stack|frame|pixel|glyph|board|piece|score|level|input",
                lw, "|")
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
        # At that density 256 KB of source is 15,460 labels and dzap runs out
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
        bytes = 0
        while (bytes < total) {
            bytes += out(form[i % n])
            i += stride
        }
        finish()
        exit 0
    }

    # real: every form once, then weighted by mnemonic.
    bytes = 0
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
    while (bytes < total) {
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
function out(line,   used) {
    used = 0
    if (ln % 8 == 0) {
        lbl++
        used += length(lname(lbl)) + 2
        print lname(lbl) ":"
    } else if (ln % 8 == 4) {
        if (int(ln / 8) % 2 == 0 && lbl > 1) {
            used += length(lname(lbl - 1)) + 6
            print "  jp " lname(lbl - 1)
        } else {
            used += length(lname(lbl + 1)) + 8
            print "  call " lname(lbl + 1)
        }
    }
    ln++
    print line
    return used + length(line) + 1
}

# The last forward reference has to land on something.
function finish() {
    print lname(lbl + 1) ":"
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

function lname(k,   a, b) {
    a = lw[(k % nlw) + 1]
    b = lw[((k * 7 + 3) % nlw) + 1]
    if (k % 3 == 0) return a "_" k "_" b
    if (k % 3 == 1) return a "_" k "_" b "_end"
    return b "_" k "_" a "_loop"
}
function gcd(a, b,  t) { while (b) { t = b; b = a % b; a = t } return a }
'
