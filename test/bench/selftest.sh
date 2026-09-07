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
labels=$(test/bench/gen_isa.sh real | grep -oE '^[A-Za-z_.][A-Za-z0-9_.]*:' \
             | sed 's/:$//')
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

exit $status
