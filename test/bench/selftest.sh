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

exit $status
