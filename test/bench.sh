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

time_run() {
    local best=99999
    for _ in 1 2 3; do
        local t0 t1 e
        t0=$(date +%s.%N)
        (cd "$EMU" && tail -f /dev/null | timeout 300 ./agon-cli-emulator \
            --sdcard "$SD" -z -u >/dev/null 2>&1; true)
        t1=$(date +%s.%N)
        e=$(echo "$t1 - $t0" | bc)
        best=$(echo "if ($e < $best) $e else $best" | bc)
    done
    echo "$best"
}

printf 'emulator_exit_success\r\n' > "$SD/autoexec.txt"
boot=$(time_run)

printf 'zap %s out.bin\r\nemulator_exit_success\r\n' "$SRC" > "$SD/autoexec.txt"
full=$(time_run)

net=$(echo "scale=3; $full - $boot" | bc)
lines=$(grep -cvE '^[[:space:]]*(;|$)' "$SD/$SRC")

echo "source     $lines statements"
echo "boot       ${boot}s"
echo "assemble   ${net}s"
echo "per stmt   $(echo "scale=1; $net * 1000000 / $lines" | bc) us"
