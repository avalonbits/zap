#!/bin/bash
# The calling convention, on the machine: C built by agondev calling assembly
# that zap assembled as an object, and the assembly calling back into C.
#
# test/object.sh shows that an object links to the right bytes. This shows
# that those bytes do the right thing when a C compiler is on the other side
# of every call -- that each argument is read from the slot agondev puts it
# in, each result comes back where agondev looks for it, and nothing C needs
# is clobbered. test/abi/asm.s says what the convention is; test/abi/src/main.c
# checks it one call at a time and prints a line for each.
#
# Needs agondev and the emulator, and skips without either. The emulator is
# driven the way zap's benchmarks drive it: a release build, run from its own
# directory, the command in autoexec.txt, stdin held open by a fifo, and both
# output streams captured.
#
#   test/abi.sh [zap]      test/run.sh passes the zap it has just built
set -uo pipefail

cd "$(dirname "$0")/.."
ROOT=$(pwd)
AGONDEV="${AGONDEV:-$HOME/agondev}"
EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"

if [ ! -x "$AGONDEV/bin/agondev-config" ]; then
    echo "SKIP  no agondev; the calling convention is not checked"
    exit 0
fi
if [ ! -x "$EMU/agon-cli-emulator" ]; then
    echo "SKIP  no emulator at $EMU; the calling convention is not checked"
    exit 0
fi

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

ZAP="${1:-}"
if [ -z "$ZAP" ]; then
    ZAP="$W/zap"
    cc -std=gnu11 -fsigned-char -O1 -include test/stubs/host_types.h -Isrc \
        -Itest/stubs -o "$ZAP" src/*.c test/stubs/agon_stubs.c || exit 1
fi

# The assembly half, as a library the C half's makefile links.
cp -r test/abi "$W/abi"
mkdir -p "$W/abi/lib"
if ! "$ZAP" -c -f elf "$W/abi/asm.s" "$W/abi/asm.o" > "$W/zap.log" 2>&1; then
    echo "FAIL  zap could not assemble test/abi/asm.s:"
    sed 's/^/      /' "$W/zap.log"
    exit 1
fi
"$AGONDEV/bin/ez80-none-elf-ar" rcs "$W/abi/lib/libasm.a" "$W/abi/asm.o"
if ! (cd "$W/abi" && PATH="$AGONDEV/bin:$PATH" make > "$W/make.log" 2>&1); then
    echo "FAIL  agondev could not build the C half:"
    sed 's/^/      /' "$W/make.log" | tail -20
    exit 1
fi

sd="$W/sd"
mkdir -p "$sd/bin"
cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
cp "$W/abi/bin/abi.bin" "$sd/bin/abi.bin"
printf 'abi\r\nemulator_exit_success\r\n' > "$sd/autoexec.txt"

fifo="$W/f"
mkfifo "$fifo"
tail -f /dev/null > "$fifo" &
hold=$!
(cd "$EMU" && timeout 120 ./agon-cli-emulator --sdcard "$sd" -z < "$fifo" > "$W/cap" 2>&1)
kill "$hold" 2>/dev/null
wait "$hold" 2>/dev/null

out=$(tr -d '\r' < "$W/cap" | grep -aE '^(PASS|FAIL|ABI) ')
if [ -z "$out" ]; then
    echo "FAIL  the program printed nothing; the emulator said:"
    tr -d '\r' < "$W/cap" | tail -15 | sed 's/^/      /'
    exit 1
fi
# Thirty lines at most: a call that wrecks the frame can send the program
# round its checks again and again before it ends.
printf '%s\n' "$out" | grep -v '^ABI ' | head -30
summary=$(printf '%s\n' "$out" | grep '^ABI ' | head -1)
if [ "$summary" != "ABI 0 FAILED" ]; then
    echo "FAIL  the calling convention: ${summary:-the program did not finish}"
    exit 1
fi
echo "PASS  every call between C and zap's object ran on the emulator"
