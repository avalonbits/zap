#!/bin/bash
# Regenerates dzap/test/cases/opcodes.s from the reference's own opcode corpus.
#
#   dzap/test/cases/gen_opcodes.sh > dzap/test/cases/opcodes.s
#
# THE ORACLE IS ez80asm, NEVER dzap. This matters more than anything else here.
#
# The previous corpus was built by running the forms through dzap and keeping
# the ones it accepted, which quietly made it a list of things dzap already got
# right. A form dzap assembled wrongly was dropped by the very filter that
# existed to catch it -- and one was: jr and djnz emitted the target truncated
# to a byte for as long as that file existed, and the file could not say so.
#
# So a line is kept when the *reference* assembles it. Whether dzap agrees is
# what test/run.sh is for, and a disagreement has to show up as a failure
# rather than as a shorter file.
#
# What is removed is removed by what the line LOOKS like, never by whether
# dzap manages it: labels, directives, the .lil/.sil/.sis/.lis suffixes, and
# operands naming a symbol. Those are features dzap has not been given yet, and
# each one arrives with its own forms. If a rule here starts dropping something
# dzap should handle, that is a bug in this script and not a licence to filter
# by behaviour again.
set -euo pipefail

cd "$(dirname "$0")/../../.."
REF=test/ref/linux_x86_64/ez80asm
[ -x "$REF" ] || { echo "no vendored ez80asm at $REF" >&2; exit 2; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# What counts as an instruction is the shared ISA table, not a list written out
# here that would drift from it. This is a feature filter and not a behavioural
# one: it says dzap implements instructions and not directives, which is true
# by construction, rather than asking dzap whether it likes a particular line.
# `dw24`, `align` and the rest fall out of it without being named.
grep -oE '\{ *"[a-z0-9.]+"' src/isa_table.c | sed -e 's/.*"\(.*\)"/\1/' | sort -u \
    > "$WORK/mnemonics"
[ -s "$WORK/mnemonics" ] || { echo "no mnemonics found in src/isa_table.c" >&2; exit 2; }

# Syntactic filter. Comments and blank lines go; so does anything whose first
# token is a directive or a label, anything carrying an ADL suffix, and any
# operand that names something rather than being a register or a literal.
collect() {
    cat test/corpus/Opcodes/tests/*.s test/corpus/Addressing/tests/*.s
    # Negative literals, which the corpus barely exercises: it has exactly one
    # negative displacement and no negative immediate at all. dzap reaches them
    # through a separate branch that negates the value, so a corpus without
    # them cannot say whether that branch works.
    cat <<'EOF'
	ld a,(ix-1)
	ld a,(ix-128)
	ld a,(iy-1)
	ld a,(iy-128)
	ld (ix-1),a
	ld (iy-128),b
	ld b,(ix-64)
	ld (ix-7),h
	adc a,(ix-3)
	add a,(iy-9)
	sub (ix-2)
	and (iy-5)
	or (ix-100)
	xor (iy-33)
	cp (ix-17)
	inc (iy-4)
	dec (ix-11)
	bit 0,(ix-1)
	set 7,(iy-128)
	res 3,(ix-99)
	rlc (iy-8)
	srl (ix-6)
	ld a,-1
	ld b,-128
	ld hl,-1
	ld de,-2
	ld bc,-32768
	adc a,-5
	sub -7
	cp -1
	and -16
	or -2
	xor -128
	add a,-100
EOF
}

collect \
| sed -e 's/;.*$//' -e 's/[[:space:]]*$//' \
| grep -vE '^[[:space:]]*$' \
| grep -E '^[[:space:]]' \
| grep -viE '^[[:space:]]*[a-z]+\.(l|s|lil|sil|sis|lis)\b' \
| grep -vE ':' \
| grep -vE '\$([^0-9a-fA-F]|$)' \
| awk -v L="$WORK/mnemonics" 'BEGIN{while((getline m < L)>0) ok[m]=1}
       {t=$1; sub(/\..*$/,"",t); if (tolower(t) in ok) print}' \
> "$WORK/candidates"

kept=0
tried=0
: > "$WORK/body"
while IFS= read -r line; do
    tried=$((tried + 1))
    printf '%s\n' "$line" > "$WORK/one.s"
    rm -f "$WORK/one.bin"
    # Assembled alone, exactly as test/run.sh assembles the whole file: no
    # .assume, no org, so the defaults under test are the ones in force.
    if "$REF" "$WORK/one.s" "$WORK/one.bin" >/dev/null 2>&1 \
       && [ -s "$WORK/one.bin" ]; then
        printf '%s\n' "$line" >> "$WORK/body"
        kept=$((kept + 1))
    fi
done < "$WORK/candidates"

cat <<EOF
; Every instruction form in the reference's own opcode corpus that ez80asm
; assembles: test/corpus/Opcodes and test/corpus/Addressing, with labels,
; directives, ADL suffixes and symbolic operands removed because dzap has none
; of those features yet. Plus negative displacements and immediates, which the
; corpus itself barely covers.
;
; KEPT BY WHAT THE REFERENCE ACCEPTS, NOT BY WHAT DZAP ACCEPTS. The earlier
; version of this file was filtered through dzap, which made it a record of
; what dzap already got right and silently dropped anything it got wrong -- jr
; and djnz were wrong for the whole life of that file and it could not say so.
;
; This is breadth the unit tests cannot reach, and it is what makes a change to
; row selection safe to make: \`ld\` alone has 57 rows, and a reordering that
; picks the wrong one for some rare addressing mode shows up here and nowhere
; else. Compared against ez80asm by test/run.sh like every other case file.
;
; Regenerate with dzap/test/cases/gen_opcodes.sh. Do not edit by hand.
; $kept forms, from $tried candidates.

EOF
cat "$WORK/body"
