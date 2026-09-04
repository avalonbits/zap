#!/bin/bash
# Measures assembly throughput on an emulated eZ80.
#
# The emulator is run with -u, so the seconds are proportional to emulated work
# rather than real Agon wall-clock; ratios hold, absolute microseconds do not.
# Boot is measured separately and subtracted.
#
# Usage: test/bench.sh <path-to-fab-agon-emulator> <path-to-sdcard> <source.asm>
#
# The source must already be on the sdcard, and zap in its /bin.
set -uo pipefail

EMU="${1:-}"; SD="${2:-}"; SRC="${3:-}"
if [ ! -x "$EMU/agon-cli-emulator" ] || [ ! -d "$SD" ] || [ ! -f "$SD/$SRC" ]; then
    echo "usage: $0 <fab-agon-emulator dir> <sdcard dir> <source on the sdcard>" >&2
    exit 2
fi

# Times three runs and returns the fastest. A run that fails or times out is
# not a measurement -- without this the elapsed time of a crash would be
# reported as a result.
time_run() {
    local best=99999
    local got=0
    for _ in 1 2 3; do
        local t0 t1 e status
        t0=$(date +%s.%N)
        # The emulator's own status, not the pipeline's. tail -f is there to
        # hold stdin open, and it is killed by SIGPIPE when the emulator
        # exits; under pipefail that 141 became the pipeline's status, so
        # every run looked like a failure and every sample was discarded.
        (cd "$EMU" && tail -f /dev/null | timeout 300 ./agon-cli-emulator \
            --sdcard "$SD" -z -u >/dev/null 2>&1; exit "${PIPESTATUS[1]}")
        status=$?
        t1=$(date +%s.%N)
        # The emulator exits non-zero when its shutdown races the VDP thread,
        # which is harmless; a timeout (124) or a signal is not.
        if [ "$status" -ge 124 ]; then
            continue
        fi
        e=$(echo "$t1 - $t0" | bc)
        best=$(echo "if ($e < $best) $e else $best" | bc)
        got=$((got + 1))
    done
    if [ "$got" -eq 0 ]; then
        echo "every emulator run failed" >&2

        # Not exit: this runs inside $( ), so exit would end the subshell and
        # leave the caller with an empty time to do arithmetic on. That is how
        # the failure above stayed invisible -- the script carried on and
        # printed "boot s" and "assemble s".
        return 1
    fi
    echo "$best"
}

printf 'emulator_exit_success\r\n' > "$SD/autoexec.txt"
boot=$(time_run) || exit 1

printf 'zap %s out.bin\r\nemulator_exit_success\r\n' "$SRC" > "$SD/autoexec.txt"
full=$(time_run) || exit 1

net=$(echo "scale=3; $full - $boot" | bc)
lines=$(grep -cvE '^[[:space:]]*(;|$)' "$SD/$SRC")
if [ "$lines" -eq 0 ]; then
    echo "source has no statements to measure" >&2
    exit 2
fi

# The emulator's elapsed time comes out quantised to about a second, so the
# resolution is a second divided by the run length: usable at 10% on a ten
# second workload, useless below that. Scale the source until the change being
# measured is comfortably larger than one second, or a real difference and no
# difference at all look identical.
echo "source     $lines statements"
echo "boot       ${boot}s"
echo "assemble   ${net}s"
echo "per stmt   $(echo "scale=1; $net * 1000000 / $lines" | bc) us"
