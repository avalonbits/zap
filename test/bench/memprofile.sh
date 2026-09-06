#!/bin/bash
# What a dzap run actually costs in memory, on the machine that has 512 KB.
#
#   test/bench/memprofile.sh [source ...]     default: the generated set
#
# MEASURED ON THE AGON, NOT THE HOST. Host figures are roughly twice the truth
# and not by a constant: a pointer is eight bytes there and three on the eZ80,
# so every struct with one in it is mis-sized differently. The symbol bucket is
# 4 bytes on target and 16 on the host; the fixup 13 and 24.
#
# Speed has been measured every round and memory has not, which is the wrong
# way round for a machine with no swap -- an assembler that is 5% faster and
# does not fit is not faster. Labels are what made it urgent: the symbol table,
# the name arena and the fixup list all grow with the source rather than being
# fixed, and the degenerate benchmark is the shape that makes the fixup list as
# large as it can be.
set -uo pipefail

cd "$(dirname "$0")/../.."
ROOT=$(pwd)
EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# A build with the shim in. zmalloc.c is symlinked into dzap/src beside the
# other shared sources and is inert without ZMALLOC, so this is the ordinary
# build plus four renames.
ZFLAGS='-DZMALLOC -Dmalloc=z_malloc -Dcalloc=z_calloc -Drealloc=z_realloc -Dfree=z_free -include src/zmalloc.h -I src'
( cd dzap && PATH="$HOME/agondev/bin:$PATH" make clean >/dev/null 2>&1
  cd "$ROOT/dzap" && PATH="$HOME/agondev/bin:$PATH" make EXTRA_CFLAGS="$ZFLAGS" ) >"$WORK/build.log" 2>&1
if [ ! -f dzap/bin/dzap.bin ]; then
    echo "the instrumented build failed:" >&2
    tail -20 "$WORK/build.log" >&2
    exit 2
fi
cp dzap/bin/dzap.bin "$WORK/dzap.bin"
# Leave the tree with an ordinary build, so a later timing run is not measuring
# the shim by accident.
( cd dzap && PATH="$HOME/agondev/bin:$PATH" make clean >/dev/null 2>&1
  cd "$ROOT/dzap" && PATH="$HOME/agondev/bin:$PATH" make ) >/dev/null 2>&1

SRCS=("$@")
if [ "${#SRCS[@]}" -eq 0 ]; then
    for m in even real degenerate memory; do
        ./test/bench/gen_isa.sh "$m" 262144 > "$WORK/isa_$m.s"
        SRCS+=("$WORK/isa_$m.s")
    done
fi

for src in "${SRCS[@]}"; do
    echo "== $(basename "$src") ($(stat -c%s "$src") bytes)"
    sd="$WORK/sd"; rm -rf "$sd"; mkdir -p "$sd/bin"
    cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
    cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
    cp "$WORK/dzap.bin" "$sd/bin/dzap.bin"
    cp "$src" "$sd/s.s"
    printf 'dzap s.s out.bin\r\nemulator_exit_success\r\n' > "$sd/autoexec.txt"
    fifo="$WORK/f"; rm -f "$fifo"; mkfifo "$fifo"
    tail -f /dev/null > "$fifo" & hold=$!
    ( cd "$EMU" && timeout 900 ./agon-cli-emulator --sdcard "$sd" -z < "$fifo" > "$WORK/cap" 2>&1 )
    kill "$hold" 2>/dev/null; wait "$hold" 2>/dev/null
    if tr -d '\r' < "$WORK/cap" | grep -q '^peak '; then
        tr -d '\r' < "$WORK/cap" | sed -n '/^peak /,/headers)/p' | sed 's/^/  /'
    else
        # An empty report is the failure mode this script is most likely to
        # have -- a build that did not take the flags, a guest that ran out of
        # memory, an emulator that never started -- and printing nothing makes
        # all three look like success. Say what the guest said instead.
        echo "  NO REPORT. What the guest printed:"
        tr -d '\r' < "$WORK/cap" \
          | grep -avE '^(Assembling |$)' \
          | grep -aE 'line [0-9]+: |Error |out of memory|Cannot |RST' \
          | head -4 | sed 's/^/    /'
        [ -s "$WORK/cap" ] || echo "    (nothing at all -- did the emulator start?)"
    fi
done
