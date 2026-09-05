#!/bin/bash
# Times zap against ez80asm on the Agon, on a fixed set of sources.
#
# The set is fixed on purpose. Timings taken on whatever source was to hand
# cannot be compared with each other a month later, and the numbers quoted in
# commit messages are worth nothing if the input has drifted. Everything here
# is either in the repository or generated deterministically from a committed
# script, so any run reproduces any earlier one.
#
# THE TIMING IS THE ASSEMBLER'S OWN. Both zap and ez80asm print "Done in X.XX
# seconds" and that is the figure used, never host wall clock -- which would
# include emulator startup, MOS boot and sdcard I/O, all of them larger than
# the difference being measured.
#
# THE EMULATOR RUNS WITHOUT -u. Unthrottling decouples the guest's clock from
# the work the guest does, so the number it prints stops meaning anything. It
# makes runs finish sooner and the results worthless.
#
# Host profiling does not substitute for this. zap retires 0.71x ez80asm's
# instructions on the host and takes 0.98x its time on an Agon: libc is
# vectorised on one machine and a byte loop on the other, and chasing struct
# pointers is cheap on x86 and expensive on a cacheless eZ80 with 3-byte
# pointers. Use callgrind to find candidates, this to size them.
#
#   test/bench/bench.sh [name ...]     default: every source in the set
#
# Needs an Agon build of zap (make) and fab-agon-emulator. The reference
# assembler for the Agon is vendored in test/ref/agon.
set -uo pipefail

cd "$(dirname "$0")/../.."
ROOT=$(pwd)

EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"
ZAP_BIN="${ZAP_BIN:-bin/zap.bin}"
EZ_BIN="test/ref/agon/ez80asm.bin"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [ ! -x "$EMU/agon-cli-emulator" ]; then
    echo "no emulator at $EMU/agon-cli-emulator; set AGON_EMU" >&2
    exit 2
fi
if [ ! -f "$ZAP_BIN" ]; then
    echo "no Agon build of zap at $ZAP_BIN; run make" >&2
    exit 2
fi

# Both binaries are copied once, here, and every source is staged from these
# copies rather than from the build tree.
#
# Staging per source instead meant a `make` while a run was in flight silently
# changed the binary halfway through it: the first source measured one build
# and the rest measured another, and the table said nothing about it. A
# benchmark that can be edited while it runs is not measuring anything.
snapshot_binaries() {
    cp "$ZAP_BIN" "$WORK/zap.bin" || exit 1
    cp "$EZ_BIN"  "$WORK/ez80asm.bin" || exit 1
}

# Above this much source, ez80asm is run with -m. See where it is used.
MEM_THRESHOLD=$((256 * 1024))

# How much source a staged sdcard holds: everything except the machine's own
# files and the ones the runner puts there itself.
source_bytes() {
    find "$1" -type f \
        ! -path "$1/bin/*" ! -path "$1/mos/*" \
        ! -name 'MOS.bin' ! -name 'firmware.bin' \
        ! -name 'autoexec.txt' ! -name 'flush.s' ! -name 'flush.bin' \
        ! -name 'out.bin' \
        -printf '%s\n' 2>/dev/null | awk '{ n += $1 } END { print n + 0 }'
}

# The set. Each entry is: name, top-level source, directory to stage.
#
# bbcbasic is the big one and the most realistic -- 20 files, deep include
# nesting, macros, and forward references that reach nearly the whole output.
# rokky is a smaller real program with a different shape. synth is generated:
# straight instructions, no labels, no macros, no includes, so it isolates
# lexing and encoding from everything else.
SETS="bbcbasic rokky synth"

stage_bbcbasic() { cp -r test/corpus/Z_PRG_Agon-bbc-basic-v/tests/* "$1/"; echo bbcbasicvez.s; }
stage_rokky()    { cp -r test/corpus/Z_PRG_Agon-Rokky/tests/*     "$1/"; echo rokky.s; }
stage_synth()    { test/bench/gen_synth.sh > "$1/synth.s";              echo synth.s; }

# Two emulators sharing one sdcard directory mutate the filesystem under each
# other. That produced an RST $38 guru meditation once that looked exactly like
# a zap bug, so this refuses to start rather than produce a number nobody can
# trust.
#
# The check walks /proc and compares the resolved executable. pgrep cannot do
# it: "agon-cli-emulator" is 17 characters and pgrep matches process names
# truncated to 15, so `pgrep -x` never matches and `pgrep -c` returns 0 no
# matter what is running -- a guard that always passes. Matching the command
# line instead (`pgrep -f`) finds this script, because the pattern is in its
# own arguments.
emu_running_here() {
    local pid exe n=0
    for pid in /proc/[0-9]*; do
        exe=$(readlink "$pid/exe" 2>/dev/null) || continue
        case "$exe" in
            */agon-cli-emulator)
                case "$(tr '\0' ' ' < "$pid/cmdline" 2>/dev/null)" in
                    *"$WORK"*) n=$((n + 1)) ;;
                esac
                ;;
        esac
    done
    echo "$n"
}

# Runs one assembler over one source and prints the seconds it reported.
#
# Returns the assembler's own figure, or nothing if it never printed one. A
# missing figure is not a fast run: it means the program died, the emulator
# panicked or the output was lost, and reporting it as zero would be worse than
# useless.
#
# MOS only runs autoexec.txt if its stdin stays open -- with stdin at EOF it
# boots to a prompt and the run produces nothing at all. So something has to
# hold the other end of the pipe open for as long as the emulator lives.
#
# That holder has to be killed explicitly. Written as `tail -f /dev/null |
# emulator`, tail never writes, so it never takes SIGPIPE when the emulator
# exits, and the shell waits on it forever: the run hangs after the work is
# already done, with no emulator left running to explain it. A fifo and an
# explicit kill make the lifetime ours to end.
run_one() {
    local sd="$1" cmd="$2" tag="$3" flush="$4"

    # A second, trivial assembly between the measured one and the shutdown.
    #
    # Without it the run ends mid-sentence: the capture reads "Wrote Emulator
    # shutdown triggered by writing 0x0 IO 0x0", because emulator_exit_success
    # takes effect while the console output of the measured run is still being
    # flushed to the VDP. The "Done in" line is simply lost, and the run looks
    # like a program that never finished rather than one whose output was cut
    # off. Assembling four lines afterwards gives the console time to drain.
    printf '  nop\n  ret\n' > "$sd/flush.s"
    printf '%s\r\n%s\r\nemulator_exit_success\r\n' \
        "$cmd" "$flush" > "$sd/autoexec.txt"

    local fifo="$WORK/stdin.$tag"
    rm -f "$fifo"
    mkfifo "$fifo" || return 1
    tail -f /dev/null > "$fifo" &
    local hold=$!

    (cd "$EMU" && timeout 900 \
        ./agon-cli-emulator --sdcard "$sd" -z < "$fifo" > "$WORK/cap.$tag" 2>&1)

    kill "$hold" 2>/dev/null
    wait "$hold" 2>/dev/null
    rm -f "$fifo"

    # The first figure is the measured run; the flush assembly prints its own
    # afterwards, which is not what anyone asked for.
    tr -d '\r' < "$WORK/cap.$tag" | grep -aoE 'Done in [0-9]+\.[0-9]+' \
        | head -1 | awk '{print $3}'
}

snapshot_binaries

want="${*:-$SETS}"

printf '%-12s %12s %12s %8s   %s\n' SOURCE ZAP EZ80ASM RATIO OUTPUT
for name in $want; do
    case " $SETS " in *" $name "*) ;; *) echo "unknown source: $name" >&2; continue ;; esac

    sd="$WORK/sd_$name"
    mkdir -p "$sd/bin"
    cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
    cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
    src=$("stage_$name" "$sd")
    cp "$WORK/zap.bin"     "$sd/bin/zap.bin"
    cp "$WORK/ez80asm.bin" "$sd/bin/ez80asm.bin"

    if [ "$(emu_running_here)" != "0" ]; then
        echo "an emulator is already using this sdcard; refusing" >&2
        exit 2
    fi

    rm -f "$sd/out.bin"
    z=$(run_one "$sd" "zap $src out.bin" "z_$name" "zap flush.s flush.bin")
    zsz=$([ -f "$sd/out.bin" ] && stat -c%s "$sd/out.bin" || echo 0)
    zmd=$([ -f "$sd/out.bin" ] && md5sum < "$sd/out.bin" | cut -c1-8 || echo "--------")

    # ez80asm gets -m only when the source is large enough to need it.
    #
    # Without -m it sizes its buffers for a desktop and never finishes on a
    # 512 KB machine: bbcbasic sat on "Pass 1..." indefinitely, while rokky --
    # a fifteenth the size -- completed normally either way, which made the
    # failure look like a hang in the runner rather than the assembler running
    # out of room.
    #
    # Passing it everywhere would be simpler but would not be a fair
    # comparison: -m costs ez80asm real time (rokky is 2.70s with it and 2.50s
    # without) and nobody reaches for it until they have to. Timing it against
    # a flag a user would not have used makes zap look better than it is. The
    # threshold is on the whole source the assembler reads, includes and all,
    # since that is what drives the memory it needs rather than the size of the
    # file named on the command line -- bbcbasic's top-level source is 554
    # bytes and its tree is 400 KB.
    staged=$(source_bytes "$sd")
    mflag=""
    if [ "$staged" -gt "$MEM_THRESHOLD" ]; then
        mflag=" -m"
    fi

    rm -f "$sd/out.bin"
    e=$(run_one "$sd" "ez80asm $src out.bin$mflag" "e_$name" "ez80asm flush.s flush.bin$mflag")
    esz=$([ -f "$sd/out.bin" ] && stat -c%s "$sd/out.bin" || echo 0)
    emd=$([ -f "$sd/out.bin" ] && md5sum < "$sd/out.bin" | cut -c1-8 || echo "--------")

    ratio="-"
    if [ -n "$z" ] && [ -n "$e" ]; then
        ratio=$(awk -v a="$z" -v b="$e" 'BEGIN { if (b > 0) printf "%.2fx", a / b; else print "-" }')
    fi

    note="$zsz bytes"
    if [ -n "$mflag" ]; then
        note="$note, ez80asm -m"
    fi
    if [ "$zmd" != "$emd" ]; then
        note="$note  MISMATCH zap=$zmd ez80asm=$emd"
    fi
    printf '%-12s %12s %12s %8s   %s\n' \
        "$name" "${z:--}" "${e:--}" "$ratio" "$note"
done
