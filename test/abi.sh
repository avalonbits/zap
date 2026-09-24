#!/bin/bash
# The calling convention, on the machine: C calling assembly that zap
# assembled as an object, and the assembly calling back into C -- once with C
# built by agondev and the object in ELF, and once with C built by acc and the
# object in acc's own format.
#
# test/object.sh shows that an object links to the right bytes. This shows
# that those bytes do the right thing when a C compiler is on the other side
# of every call -- that each argument is read from the slot the compiler puts
# it in, each result comes back where it looks for it, and nothing C needs is
# clobbered. test/abi/asm.s says what the convention is; test/abi/src/main.c
# checks it one call at a time and prints a line for each. The two compilers
# share the convention, so the same two files serve both.
#
# Needs the emulator, and agondev or acc (from ACC_REPO, ~/code/acc by
# default, built); each toolchain that is missing is skipped. The emulator is
# driven the way zap's benchmarks drive it: a release build, run from its own
# directory, the command in autoexec.txt, stdin held open by a fifo, and both
# output streams captured.
#
#   test/abi.sh [zap]      test/run.sh passes the zap it has just built
set -uo pipefail

cd "$(dirname "$0")/.."
AGONDEV="${AGONDEV:-$HOME/agondev}"
ACC_REPO="${ACC_REPO:-$HOME/code/acc}"
EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"

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

status=0

# Runs one program on the emulator; its console output is left in
# $W/cap-$1. $2 is the binary.
emu_run() {
    local name="$1" bin="$2"
    local sd="$W/sd-$name"
    rm -rf "$sd"
    mkdir -p "$sd/bin"
    cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
    cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
    cp "$bin" "$sd/bin/abi.bin"
    printf 'abi\r\nemulator_exit_success\r\n' > "$sd/autoexec.txt"

    local fifo="$W/f-$name"
    mkfifo "$fifo"
    tail -f /dev/null > "$fifo" &
    local hold=$!
    (cd "$EMU" && timeout 120 ./agon-cli-emulator --sdcard "$sd" -z < "$fifo" > "$W/cap-$name" 2>&1)
    kill "$hold" 2>/dev/null
    wait "$hold" 2>/dev/null
}

# Runs test/abi's program and reads its report.
run_program() {
    local name="$1" bin="$2"
    emu_run "$name" "$bin"

    local out
    out=$(tr -d '\r' < "$W/cap-$name" | grep -aE '^(PASS|FAIL|ABI) ')
    if [ -z "$out" ]; then
        echo "FAIL  $name: the program printed nothing; the emulator said:"
        tr -d '\r' < "$W/cap-$name" | tail -15 | sed 's/^/      /'
        status=1
        return
    fi
    # Thirty lines at most: a call that wrecks the frame can send the program
    # round its checks again and again before it ends.
    printf '%s\n' "$out" | grep -v '^ABI ' | head -30 | sed "s/^\(PASS\|FAIL\) /\1 $name: /"
    local summary
    summary=$(printf '%s\n' "$out" | grep '^ABI ' | head -1)
    if [ "$summary" != "ABI 0 FAILED" ]; then
        echo "FAIL  $name: ${summary:-the program did not finish}"
        status=1
        return
    fi
    echo "PASS  $name: every call between C and zap's object ran on the emulator"
}

# agondev, with the object in ELF, linked as a library the makefile names.
if [ -x "$AGONDEV/bin/agondev-config" ]; then
    cp -r test/abi "$W/agondev"
    mkdir -p "$W/agondev/lib"
    if ! "$ZAP" -c -f elf "$W/agondev/asm.s" "$W/agondev/asm.o" > "$W/zap.log" 2>&1; then
        echo "FAIL  agondev: zap could not assemble test/abi/asm.s:"
        sed 's/^/      /' "$W/zap.log"
        status=1
    else
        "$AGONDEV/bin/ez80-none-elf-ar" rcs "$W/agondev/lib/libasm.a" "$W/agondev/asm.o"
        if ! (cd "$W/agondev" && PATH="$AGONDEV/bin:$PATH" make > "$W/make.log" 2>&1); then
            echo "FAIL  agondev could not build the C half:"
            sed 's/^/      /' "$W/make.log" | tail -20
            status=1
        else
            run_program agondev "$W/agondev/bin/abi.bin"
        fi
    fi
else
    echo "SKIP  no agondev; the calling convention with agondev is not checked"
fi

# acc, with the object in acc's format, linked with its C library.
ACC="$ACC_REPO/bin/acc"
if [ -x "$ACC" ] && [ -f "$ACC_REPO/bin/libc.a" ]; then
    if ! "$ZAP" -c -f acc test/abi/asm.s "$W/asm_acc.o" > "$W/zap.log" 2>&1; then
        echo "FAIL  acc: zap could not assemble test/abi/asm.s:"
        sed 's/^/      /' "$W/zap.log"
        status=1
    elif ! "$ACC" -c test/abi/src/main.c -o "$W/main_acc.o" -I "$ACC_REPO/include" \
            > "$W/acc.log" 2>&1 \
         || ! "$ACC" "$W/main_acc.o" "$W/asm_acc.o" "$ACC_REPO/bin/libc.a" \
            -o "$W/abi_acc.bin" >> "$W/acc.log" 2>&1; then
        echo "FAIL  acc could not build the C half:"
        sed 's/^/      /' "$W/acc.log" | tail -20
        status=1
    else
        run_program acc "$W/abi_acc.bin"
    fi
else
    echo "SKIP  no acc at $ACC_REPO; the calling convention with acc is not checked"
fi

# The example in docs/examples, built both ways exactly as
# docs/zap-with-agondev.md and docs/zap-with-acc.md say, so that what the
# guides tell a reader to type is known to work.
run_example() {
    local name="$1" bin="$2"
    emu_run "$name" "$bin"
    if tr -d '\r' < "$W/cap-$name" | grep -aq '^all correct$'; then
        echo "PASS  $name: the example in docs/examples runs"
    else
        echo "FAIL  $name: the example in docs/examples did not say it was correct:"
        tr -d '\r' < "$W/cap-$name" | tail -8 | sed 's/^/      /'
        status=1
    fi
}
if [ -x "$AGONDEV/bin/agondev-config" ]; then
    cp -r docs/examples "$W/ex-agondev"
    mkdir -p "$W/ex-agondev/lib"
    if (cd "$W/ex-agondev" \
        && "$ZAP" bytes.s bytes.o -f elf \
        && "$AGONDEV/bin/ez80-none-elf-ar" rcs lib/libbytes.a bytes.o \
        && PATH="$AGONDEV/bin:$PATH" make) > "$W/ex-agondev.log" 2>&1; then
        run_example example-agondev "$W/ex-agondev/bin/bytes.bin"
    else
        echo "FAIL  the example does not build with agondev:"
        tail -10 "$W/ex-agondev.log" | sed 's/^/      /'
        status=1
    fi
fi
if [ -x "$ACC" ] && [ -f "$ACC_REPO/bin/libc.a" ]; then
    cp -r docs/examples "$W/ex-acc"
    if (cd "$W/ex-acc" \
        && "$ZAP" bytes.s bytes.o -f acc \
        && "$ACC" -c src/main.c -o main.o -I "$ACC_REPO/include" \
        && "$ACC" main.o bytes.o "$ACC_REPO/bin/libc.a" -o bytes.bin) \
        > "$W/ex-acc.log" 2>&1; then
        run_example example-acc "$W/ex-acc/bytes.bin"
    else
        echo "FAIL  the example does not build with acc:"
        tail -10 "$W/ex-acc.log" | sed 's/^/      /'
        status=1
    fi
fi

exit $status
