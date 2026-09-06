#!/bin/bash
# Generates a source containing every instruction form dzap can assemble.
#
#   test/bench/gen_isa.sh even [bytes] > isa_even.s
#   test/bench/gen_isa.sh real [bytes] > isa_real.s
#
# gen_pure.sh cycles forty hand-picked forms, which is a plausible instruction
# stream but not a sample of the instruction set: it reaches 31 of the ISA's
# 114 mnemonics and 40 of its 322 rows, with no call, jp or djnz at all. A
# change that helps the shapes it happens to contain and hurts the ones it does
# not will still look like an improvement. That is not hypothetical -- it
# happened, and is written up in .internal/performance-notes.md.
#
# The forms come from dzap/test/cases/opcodes.s, which is the reference's own
# opcode corpus filtered to what dzap assembles byte-identically, plus the call
# forms that corpus happens not to contain. Deriving them from a file that is
# already checked against ez80asm on every test run means these cannot drift
# into containing something dzap gets wrong.
#
# TWO DISTRIBUTIONS, because neither alone answers the question:
#
#   even  Every form the same number of times. Says what the instruction set
#         costs. Nothing is over-weighted, so a change cannot look good by
#         helping whatever happens to be common here.
#
#   real  Weighted by how often each mnemonic appears in the two real programs
#         in test/corpus -- BBC BASIC and Rokky, 10,440 instructions between
#         them -- while still containing every form at least once. Says what
#         the assembler costs on the kind of source people actually feed it.
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
# script invalidates every timing taken with it.
set -euo pipefail

cd "$(dirname "$0")/../.."

MODE="${1:?usage: gen_isa.sh <even|real> [bytes]}"
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
            line = form[i % n]
            print line
            bytes += length(line) + 1
            i += stride
        }
        exit 0
    }

    # real: every form once, then weighted by mnemonic.
    bytes = 0
    for (i = 0; i < n && bytes < total; i++) {
        print form[i]
        bytes += length(form[i]) + 1
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
        line = bucket[m, use[m]++ % cnt[m]]
        print line
        bytes += length(line) + 1
    }
}
function gcd(a, b,  t) { while (b) { t = b; b = a % b; a = t } return a }
'
