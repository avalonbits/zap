#!/bin/bash
# Tests the bench rig's own failure detection, without an emulator.
#
# The rig has to tell a slow run from a dead one. When it cannot, a guest that
# failed in the first thirty seconds sits at a MOS prompt until the fifteen
# minute timeout and looks exactly like a long assembly -- which is how twenty
# minutes once went into waiting for an answer that had already arrived.
#
# These are real captures, trimmed: what the console actually holds after a
# successful run, after each way a run has failed so far, and the one case that
# must NOT be read as failure -- a source whose own name contains something the
# patterns look for.
set -uo pipefail

cd "$(dirname "$0")/../.."

# Pull the two predicates out of the rig rather than copying them, so this
# tests what runs and not a stale duplicate.
eval "$(sed -n '/^guest_failed() {/,/^}/p' test/bench/bench.sh)"
eval "$(sed -n '/^guest_error() {/,/^}/p' test/bench/bench.sh)"

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
status=0

check() {
    local name="$1" want="$2" body="$3"
    printf '%b' "$body" > "$W/cap"
    if guest_failed "$W/cap"; then got=fail; else got=ok; fi
    if [ "$got" = "$want" ]; then
        echo "PASS  $name"
    else
        echo "FAIL  $name: read as '$got', want '$want'"
        status=1
    fi
}

check "a clean run" ok \
    'Assembling synth.s\r\nWrote out.bin, 46480 bytes\r\nDone in 6.70 seconds\r\n'

check "an assembler error" fail \
    'Assembling s.s\r\ns.s line 2: unexpected text after the instruction\r\n'

check "MOS abandoning autoexec" fail \
    'Assembling s.s\r\nError executing autoexec.txt at line 1\r\n'

check "an unreadable sdcard" fail \
    'Error accessing SD card\r\n/ *\r\n'

check "the output file refused" fail \
    'Assembling s.s\r\nCannot write out.bin\r\n'

check "a guru meditation" fail \
    'Assembling s.s\r\nRST $38 guru meditation\r\n'

check "an empty capture" ok ''

# The patterns must not fire on ordinary output that happens to contain their
# words. A source called "inline 12: notes.s" is legal and would otherwise
# look like an assembler complaining about line 12.
check "a filename that reads like an error" ok \
    'Assembling deadline 12: notes.s\r\nWrote out.bin, 4 bytes\r\nDone in 0.01 seconds\r\n'

# And the message has to reach the operator, not just the exit path.
printf 'Assembling s.s\r\ns.s line 2: unexpected text after the instruction\r\n' > "$W/cap"
msg=$(guest_error "$W/cap")
case "$msg" in
    *"line 2: unexpected text"*) echo "PASS  the error is reported" ;;
    *) echo "FAIL  the error is reported: got '$msg'"; status=1 ;;
esac

# gen_isa.sh's label names, which are a benchmark input and so are checked here
# rather than in dzap's suite.
#
# The names decide what the symbol table appears to cost, and they were wrong
# for three revisions of that script: two words and an index, mean 17.1
# characters, never shorter than eleven, against the Agon corpus's mean of 8.5
# and median of 7. Every symbol-table figure taken over those files reads about
# twice what it should.
# EQU definitions are filtered out: `eq17:` is a name too, but it comes from the
# directive generator and not from lname, and letting it in dragged the mean
# from 8.37 to 6.59 the moment the directives arrived.
labels=$(test/bench/gen_isa.sh real | grep -v ' EQU ' \
             | grep -oE '^[A-Za-z_.][A-Za-z0-9_.]*:' | sed 's/:$//')
nlabels=$(printf '%s\n' "$labels" | wc -l)

# Unique, which is what makes the file assemble at all: a second definition of
# a name is an error, and the generator has no way to notice it produced one.
ndistinct=$(printf '%s\n' "$labels" | sort -u | wc -l)
if [ "$nlabels" -gt 400 ] && [ "$nlabels" -eq "$ndistinct" ]; then
    echo "PASS  every generated label name is distinct"
else
    echo "FAIL  every generated label name is distinct: $nlabels names, $ndistinct distinct"
    status=1
fi

# The corpus mean is 8.45 over 20,865 definitions and the table is built to
# reproduce it. A band rather than a number, because the count of labels in the
# file depends on how the byte budget happens to fall.
mean=$(printf '%s\n' "$labels" | awk '{t += length($0)} END {printf "%.2f", t / NR}')
if awk -v m="$mean" 'BEGIN { exit !(m >= 8.0 && m <= 8.9) }'; then
    echo "PASS  label names average the length the corpus does ($mean)"
else
    echo "FAIL  label names average the length the corpus does: got $mean, want 8.0..8.9"
    status=1
fi

# The mean alone does not say the distribution is right -- the old generator
# could have hit it with every name the same length. A tenth of the corpus is
# four characters or fewer and a tenth is fourteen or more, so both ends have to
# be there.
short=$(printf '%s\n' "$labels" | awk 'length($0) <= 4' | wc -l)
long=$(printf '%s\n' "$labels" | awk 'length($0) >= 14' | wc -l)
if [ "$short" -gt 0 ] && [ "$long" -gt 0 ]; then
    echo "PASS  both ends of the length distribution appear ($short short, $long long)"
else
    echo "FAIL  both ends of the length distribution appear: $short short, $long long"
    status=1
fi

# And none of them may also read as a number. A run of hexadecimal digits with a
# trailing h is a literal to the reference, which then refuses it as a label --
# the ambiguity the operand parser resolves in favour of the number.
if ! printf '%s\n' "$labels" | grep -qiE '^[0-9a-f]+h$'; then
    echo "PASS  no generated label reads as a hexadecimal literal"
else
    echo "FAIL  no generated label reads as a hexadecimal literal"
    status=1
fi

# The features the two ISA sources are supposed to exercise. Until the fifth
# revision of gen_isa.sh neither file held a single directive, so every one of
# those code paths was being measured at zero cost by a source that never
# reached it -- and isa_real read 342 cycles a byte instead of 401.
isareal=$(test/bench/gen_isa.sh real)
for want in 'DB ' 'DW ' 'DL ' 'DS ' 'ALIGN ' 'ORG ' ' EQU '; do
    n=$(printf '%s\n' "$isareal" | grep -c "$want" || true)
    if [ "$n" -gt 0 ]; then
        echo "PASS  isa_real contains $want ($n lines)"
    else
        echo "FAIL  isa_real contains $want: none"
        status=1
    fi
done

# The values those EQUs take. They were all expressions once, on the reasoning
# that a value which is only a number never reaches the evaluator -- and four in
# five are exactly that in the corpus, so the file was making every EQU four
# times harder than the average real one. Both ends have to be there: without
# the literals it measures an evaluator nobody runs, and without the
# expressions it stops measuring the evaluator at all.
equvals=$(printf '%s\n' "$isareal" | sed -n 's/^[^ ]*: EQU //p')
nequ=$(printf '%s\n' "$equvals" | grep -c . || true)
for kind in '^0x' '^[0-9][0-9]*$' '^%[01]' "^'" '[]+*[]'; do
    n=$(printf '%s\n' "$equvals" | grep -c -- "$kind" || true)
    if [ "$n" -gt 0 ]; then
        echo "PASS  isa_real has EQU values matching $kind ($n)"
    else
        echo "FAIL  isa_real has EQU values matching $kind: none"
        status=1
    fi
done

# And roughly the corpus proportions: hexadecimal is most of them and
# expressions are about a fifth. A band rather than a number, because the
# sample is a few turns of a hundred-slot cycle and a partial turn skews it.
nhex=$(printf '%s\n' "$equvals" | grep -c '^0x' || true)
nexp=$(printf '%s\n' "$equvals" | grep -c -- '[]+*[]' || true)
if [ "$nequ" -gt 100 ] \
   && [ $((nhex * 100 / nequ)) -ge 55 ] && [ $((nhex * 100 / nequ)) -le 80 ] \
   && [ $((nexp * 100 / nequ)) -ge 10 ] && [ $((nexp * 100 / nequ)) -le 30 ]; then
    echo "PASS  the EQU mix is about the corpus's ($((nhex * 100 / nequ))% hex, $((nexp * 100 / nequ))% expressions)"
else
    echo "FAIL  the EQU mix is about the corpus's: $nhex hex and $nexp expressions of $nequ"
    status=1
fi

# And the two it must NOT contain: INCLUDE and INCBIN have a source of their
# own, because their cost is file opening rather than assembling.
if ! printf '%s\n' "$isareal" | grep -qE 'INCLUDE|INCBIN'; then
    echo "PASS  isa_real leaves the file directives to isa_include"
else
    echo "FAIL  isa_real leaves the file directives to isa_include"
    status=1
fi

# The include tree: ten source files, three blobs, and a shape that is a tree
# rather than a chain. A chain only pushes readers and then pops them all; the
# case worth testing is a parent that still has lines left when a child ends.
INCW=$(mktemp -d)
test/bench/gen_isa.sh include "$INCW" > /dev/null
nsrc=$(ls "$INCW"/*.inc "$INCW"/isa_include.s 2>/dev/null | wc -l)
nbin=$(ls "$INCW"/*.bin 2>/dev/null | wc -l)
if [ "$nsrc" -eq 10 ] && [ "$nbin" -eq 3 ]; then
    echo "PASS  the include tree is ten sources and three blobs"
else
    echo "FAIL  the include tree is ten sources and three blobs: got $nsrc and $nbin"
    status=1
fi

# Branching, not a chain: at least one file has to include two others, or the
# tree is a line drawn sideways.
branching=$(grep -c 'INCLUDE' "$INCW"/isa_include.s || true)
twokids=0
for f in "$INCW"/isa_include.s "$INCW"/*.inc; do
    [ "$(grep -c 'INCLUDE' "$f" || true)" -ge 2 ] && twokids=$((twokids + 1))
done
if [ "$branching" -ge 2 ] && [ "$twokids" -ge 3 ]; then
    echo "PASS  the tree branches ($twokids files include two others)"
else
    echo "FAIL  the tree branches: root has $branching, $twokids files include two"
    status=1
fi

# Four levels deep, which is what makes a parent resume with lines still to go.
depth=0
f="$INCW/isa_include.s"
while [ -n "$f" ]; do
    depth=$((depth + 1))
    next=$(grep -oE 'INCLUDE "[^"]+"' "$f" | head -1 | sed 's/.*"\(.*\)"/\1/')
    [ -n "$next" ] && f="$INCW/$next" || f=""
done
if [ "$depth" -ge 4 ]; then
    echo "PASS  the tree is $depth levels deep"
else
    echo "FAIL  the tree is at least four levels deep: got $depth"
    status=1
fi

if grep -qE 'INCBIN' "$INCW"/*.inc "$INCW"/isa_include.s; then
    echo "PASS  the tree mixes INCBIN with INCLUDE"
else
    echo "FAIL  the tree mixes INCBIN with INCLUDE"
    status=1
fi

inctext=$(cat "$INCW"/*.inc "$INCW"/isa_include.s | wc -c)
if [ "$inctext" -ge 262144 ] && [ "$inctext" -lt 280000 ]; then
    echo "PASS  the tree holds 256 KiB of text ($inctext bytes)"
else
    echo "FAIL  the tree holds 256 KiB of text: got $inctext"
    status=1
fi
rm -rf "$INCW"

exit $status
