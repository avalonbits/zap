#!/bin/bash
# zap built by acc, on the machine: BBC BASIC assembled to the bytes the
# corpus expects.
#
# zap ships built by agondev, and everything else in test/run.sh runs the
# host build. acc compiles the same sources differently -- a local declared
# `register` lives in IY there, where agondev ignores the keyword -- so this
# builds zap with acc and has that zap assemble the largest source in the
# corpus. It is also the source zap's timings are taken on, so what it runs
# is what the annotations were measured for. -ez80 because the expected
# bytes are the reference's, as in test/corpus.sh.
#
# Needs the emulator and acc (from ACC_REPO, ~/code/acc by default, built);
# either missing is a skip. The emulator is driven as test/abi.sh drives it.
#
#   test/acc-build.sh
set -uo pipefail

cd "$(dirname "$0")/.."
ACC_REPO="${ACC_REPO:-$HOME/code/acc}"
ACC="$ACC_REPO/bin/acc"
EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"
SRC=test/corpus/Z_PRG_Agon-bbc-basic-v/tests

if [ ! -x "$EMU/agon-cli-emulator" ]; then
    echo "SKIP  no emulator at $EMU; zap built by acc is not checked"
    exit 0
fi
if [ ! -x "$ACC" ] || [ ! -f "$ACC_REPO/bin/libc.a" ]; then
    echo "SKIP  no acc at $ACC_REPO; zap built by acc is not checked"
    exit 0
fi

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

for f in src/*.c; do
    if ! "$ACC" -c "$f" -o "$W/$(basename "$f" .c).o" -I "$ACC_REPO/include" \
            -I src -DAGONDEV > "$W/acc.log" 2>&1; then
        echo "FAIL  acc could not compile $f:"
        sed 's/^/      /' "$W/acc.log" | tail -10
        exit 1
    fi
done
if ! "$ACC" "$W"/*.o "$ACC_REPO/bin/libc.a" -o "$W/zap.bin" > "$W/acc.log" 2>&1; then
    echo "FAIL  acc could not link zap:"
    sed 's/^/      /' "$W/acc.log" | tail -10
    exit 1
fi

sd="$W/sd"
mkdir -p "$sd/bin"
cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
cp "$W/zap.bin" "$sd/bin/zap.bin"
cp "$SRC"/*.s "$SRC"/*.inc "$sd/"
printf 'zap -ez80 bbcbasicvez.s out.bin\r\nemulator_exit_success\r\n' > "$sd/autoexec.txt"

fifo="$W/fifo"
mkfifo "$fifo"
tail -f /dev/null > "$fifo" &
hold=$!
(cd "$EMU" && timeout 180 ./agon-cli-emulator --sdcard "$sd" -z < "$fifo" > "$W/cap" 2>&1)
kill "$hold" 2>/dev/null
wait "$hold" 2>/dev/null

if [ ! -f "$sd/out.bin" ]; then
    echo "FAIL  zap built by acc wrote nothing for BBC BASIC; the emulator said:"
    tr -d '\r' < "$W/cap" | tail -10 | sed 's/^/      /'
    exit 1
fi
if ! cmp -s "$sd/out.bin" "$SRC/bbcbasicvez.expect"; then
    echo "FAIL  zap built by acc assembled BBC BASIC to different bytes"
    exit 1
fi
echo "PASS  zap built by acc assembles BBC BASIC to the expected bytes"
