#!/bin/bash
# Times one binary on one source, and prints the seconds it reported.
#
#   test/bench/time-one.sh <assembler.bin> <source.s>
#
# bench.sh times the fixed set with the build in the tree. This times an
# arbitrary binary against an arbitrary source, which is what comparing two
# variants of a change needs: build both, keep both, and run them against the
# same file without a rebuild in between. Every "measured on its own" figure in
# .internal/dzap-to-zap.md was taken this way.
#
# The same two rules as bench.sh, for the same reasons. THE TIMING IS THE
# ASSEMBLER'S OWN -- the "Done in" line it prints, never host wall clock, which
# would include emulator startup and MOS boot. And THE EMULATOR RUNS WITHOUT -u,
# because unthrottling decouples the guest's clock from the work it does and
# the number stops meaning anything.
#
# Run variants one after another, not at once: two emulators share the host's
# CPU and both numbers drift.
#
# Prints the output's md5 to stderr, because a variant that is faster and wrong
# is the failure mode this exists to catch.
set -uo pipefail

EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"
BIN="${1:?usage: time-one.sh <assembler.bin> <source.s>}"
SRC="${2:?usage: time-one.sh <assembler.bin> <source.s>}"

if [ ! -x "$EMU/agon-cli-emulator" ]; then
    echo "no emulator at $EMU/agon-cli-emulator; set AGON_EMU" >&2
    exit 2
fi

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

sd="$W/sd"
mkdir -p "$sd/bin"
cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
cp "$BIN" "$sd/bin/dzap.bin"
cp "$SRC" "$sd/s.s"
printf '  nop\n  ret\n' > "$sd/flush.s"
printf 'dzap s.s out.bin\r\ndzap flush.s flush.bin\r\nemulator_exit_success\r\n' \
    > "$sd/autoexec.txt"

# Shared with bench.sh so the two cannot drift apart.
eval "$(sed -n '/^guest_error() {/,/^}/p' "$(dirname "$0")/bench.sh")"

fifo="$W/f"
mkfifo "$fifo"
tail -f /dev/null > "$fifo" &
hold=$!

: > "$W/cap"
(cd "$EMU" && timeout 900 \
    ./agon-cli-emulator --sdcard "$sd" -z < "$fifo" > "$W/cap" 2>&1) &
emu=$!

# Ends the run as soon as the guest says it failed, rather than leaving it at a
# MOS prompt until the timeout, where a dead run looks exactly like a slow one.
(
    while kill -0 "$emu" 2>/dev/null; do
        if guest_error "$W/cap" | grep -qa .; then
            kill -9 "$emu" 2>/dev/null
            break
        fi
        sleep 0.5
    done
) &
watch=$!

wait "$emu" 2>/dev/null
kill "$watch" 2>/dev/null; wait "$watch" 2>/dev/null
kill "$hold" 2>/dev/null;  wait "$hold" 2>/dev/null

if guest_error "$W/cap" | grep -qa .; then
    echo "the guest failed --" >&2
    guest_error "$W/cap" | sed 's/^/  /' >&2

    exit 1
fi

tr -d '\r' < "$W/cap" | grep -aoE 'Done in [0-9]+\.[0-9]+' | head -1 | awk '{print $3}'
[ -f "$sd/out.bin" ] && md5sum < "$sd/out.bin" | cut -c1-8 >&2
