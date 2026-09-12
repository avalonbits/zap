#!/bin/bash
# Builds the hardware test kit: everything that has to go on an Agon's SD card
# to measure the output window on real silicon, and an Obey script that runs it.
#
# WHY A KIT AND NOT A BENCHMARK. test/bench/bench.sh measures two assemblers on
# fixed sources through the emulator, which is exact on CPU and has been checked
# against hardware. What it cannot measure is the *card*: the window writes the
# output as it fills, reads chunks of it back to patch them, and the cost of
# that is FatFS on a real SD card and not an emulated one. So this runs on the
# machine itself and brings a log back.
#
# WHY THE ASSEMBLERS WRITE THE LOG. MOS has no output redirection and a program
# cannot run another program, so nothing can wrap a run and record what it took.
# The only thing that can write a timing into a file is whatever measured it.
# Both assemblers are therefore built with -DKITLOG, which appends one line to
# kit.log after the clock is read -- for zap that is a build flag on the
# ordinary source, and for ez80asm it is the only change from v2.2 as released.
# Neither is the binary anyone ships.
#
#   test/hwkit.sh [outdir]        default hwkit/
#
# Then copy the whole of it onto the card and, on the Agon:
#
#   *Obey -v kit.obey
#
# -v echoes each line before it runs, so the screen shows what is happening
# while kit.log collects what it cost. Obey is MOS 3.0 or above.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT="${1:-$ROOT/hwkit}"

AGONDEV="${AGONDEV:-$HOME/agondev}"
EZSRC="${EZSRC:-$HOME/code/ez80asm-opt}"
EZBASE=241abaa                       # "Release v2.2", the stock reference

if [ ! -x "$AGONDEV/bin/agondev-config" ]; then
    echo "no agondev toolchain at $AGONDEV; set AGONDEV" >&2
    exit 2
fi
export PATH="$PATH:$AGONDEV/bin"

rm -rf "$OUT"
mkdir -p "$OUT/bin"

# ----------------------------------------------------------------------
# The binaries.
#
# One zap per window size, because the window is fixed at build time -- there
# is no flag for it, and there should not be: it is a constant the compiler
# uses to size the one allocation the assembler makes.
# ----------------------------------------------------------------------
WINDOWS="512 1024 2048 4096 8192 16384 32768 65536"
NAMES="zw512 zw1k zw2k zw4k zw8k zw16k zw32k zw64k"

set -- $NAMES
for w in $WINDOWS; do
    name=$1; shift
    echo "building $name (window $w)"
    make -s clean > /dev/null 2>&1 || true
    make -s EXTRA_CFLAGS="-DKITLOG -DOUT_WINDOW=$w" > /dev/null
    cp bin/zap.bin "$OUT/bin/$name.bin"
done
# Put the tree back the way it was found. Without this the last thing the loop
# above did was a clean, and bin/zap.bin -- which is committed -- is gone.
make -s clean > /dev/null 2>&1 || true
make -s > /dev/null

echo "building ezlog (stock ez80asm v2.2 plus the log line)"
EZW=$(mktemp -d)
trap 'rm -rf "$EZW"' EXIT
git -C "$EZSRC" worktree add -q --detach "$EZW/src" "$EZBASE"
python3 - "$EZW/src" <<'PY'
import sys
d = sys.argv[1]
p = d + "/src/main.c"
s = open(p).read()
old = '''    if(errorcount) return EXIT_ERROR;
    else printf("Done in %.2f seconds\\n",((double)(end - begin) / CLOCKS_PER_SEC));'''
new = old + '''

#ifdef KITLOG
    kit_log(inputfilename, ((double)(end - begin) / CLOCKS_PER_SEC));
#endif'''
assert s.count(old) == 1
s = s.replace(old, new)
old2 = "int main(int argc, char *argv[]) {"
new2 = '''#ifdef KITLOG
#ifndef KITLOG_FILE
#define KITLOG_FILE "kit.log"
#endif
#define KITLOG_MAX 8192

/* Appends by reading the file and writing it back: this MOS refuses
 * FA_OPEN_APPEND and FA_OPEN_ALWAYS, both of which hand back a zero handle.
 * The only change from ez80asm v2.2 as released, and it runs after the clock
 * has been read. */
static void kit_log(const char* src, double secs) {
    static char buf[KITLOG_MAX];
    int n = 0;
    uint8_t fh = mos_fopen(KITLOG_FILE, FA_READ);
    if(fh != 0) {
        n = (int) mos_fread(fh, buf, (uint24_t)(KITLOG_MAX - 256));
        mos_fclose(fh);
        if(n < 0) n = 0;
    }
    n += sprintf(buf + n, "ez80asm src=%s t=%.2f\\r\\n", src, secs);
    fh = mos_fopen(KITLOG_FILE, FA_WRITE | FA_CREATE_ALWAYS);
    if(fh == 0) return;
    mos_fwrite(fh, buf, (uint24_t) n);
    mos_fclose(fh);
}
#endif

''' + old2
assert s.count(old2) == 1
s = s.replace(old2, new2)
s = s.replace("#include <time.h>", "#include <time.h>\n#ifdef KITLOG\n#include <agon/mos.h>\n#endif", 1)
open(p, "w").write(s)
PY
printf '\nCFLAGS += $(EXTRA_CFLAGS)\n' >> "$EZW/src/Makefile-agon"
( cd "$EZW/src" && make -f Makefile-agon -s EXTRA_CFLAGS=-DKITLOG > /dev/null )
cp "$EZW/src/bin/ez80asm.bin" "$OUT/bin/ezlog.bin"
git -C "$EZSRC" worktree remove --force "$EZW/src"

# ----------------------------------------------------------------------
# The sources.
# ----------------------------------------------------------------------
# (1) One source, isa_real's own instruction mix, sized for about 96 KiB of
#     output -- small enough that the assembler as it was before the window
#     could still do it, so the window sweep is measuring the window and not
#     the difference between working and not.
echo "generating part 1"
test/bench/gen_isa.sh real 497000 > "$OUT/p1w.s"

# (2) Output size alone. A handful of BLKB lines, so the source is nothing and
#     what is being measured is writing the output and sweeping it. No forward
#     references at all: this is the floor.
echo "generating part 2"
p2() {
    printf '; Generated by test/hwkit.sh -- output size with nothing else.\r\n' > "$2"
    printf '    .assume adl=1\r\n    .org 0x40000\r\n' >> "$2"
    left=$1
    while [ "$left" -gt 0 ]; do
        take=$(( left > 4096 ? 4096 : left ))
        printf '    blkb %d, 0x5A\r\n' "$take" >> "$2"
        left=$(( left - take ))
    done
    printf '    ret\r\n' >> "$2"
}
p2 32768   "$OUT/p2-32k.s"
p2 65536   "$OUT/p2-64k.s"
p2 131072  "$OUT/p2-128k.s"
p2 262144  "$OUT/p2-256k.s"
p2 524288  "$OUT/p2-512k.s"
p2 1048576 "$OUT/p2-1m.s"

# (3) The same sizes with isa_real's label density and every definition at the
#     end of the file, so every reference is settled as late as it can be and
#     every patch lands behind the window. See test/gen_worst.sh.
echo "generating part 3 (these are large)"
for pair in "32768 32k" "65536 64k" "131072 128k" "262144 256k" "524288 512k" "1048576 1m"; do
    set -- $pair
    test/gen_worst.sh "$1" > "$OUT/p3-$2.s"
done

# ----------------------------------------------------------------------
# The script.
#
# *Try around every run, because Obey stops the whole file when a command
# fails and some of these are *expected* to fail: ez80asm holds its output in
# memory, so the larger sizes have nowhere to go. That failure is a result and
# not an accident, and the run has to carry on past it.
# ----------------------------------------------------------------------
# ez80asm holds its source and its output in memory, and refuses anything
# large with "Error allocating memory; try the -m option". -m is how it is run
# on anything sizeable -- test/bench/bench.sh uses the same 256 KB threshold on
# the source, and BASELINE.md's figures for bbcbasic and synth are -m figures.
#
# Leaving it out is not a fair comparison, it is no comparison: the run simply
# fails and writes no line. That is what happened the first time this kit ran,
# and it cost the part 1 baseline and all of part 3 above 32 KiB.
EZMEM=$((256 * 1024))
ezflag() {
    if [ "$(stat -c%s "$OUT/$1")" -gt "$EZMEM" ]; then
        printf -- ' -m'
    fi
}

echo "writing kit.obey"
{
    printf 'Echo\r\n'
    printf 'Echo === zap output-window kit ===\r\n'
    printf 'Echo Timings land in kit.log. Each line says which assembler,\r\n'
    printf 'Echo which window and which source, so the log needs no headings.\r\n'
    printf 'Echo\r\n'

    printf 'Echo -- 1. one 96 KiB output, every window size --\r\n'
    for n in $NAMES; do
        printf 'Try %s -ez80 p1w.s o-p1-%s.bin\r\n' "$n" "$n"
    done
    printf 'Try ezlog%s p1w.s o-p1-ez.bin\r\n' "$(ezflag p1w.s)"
    printf 'Echo\r\n'

    printf 'Echo -- 2. output size alone, no forward references --\r\n'
    for s in 32k 64k 128k 256k 512k 1m; do
        printf 'Try zw64k -ez80 p2-%s.s o-p2-%s-z.bin\r\n' "$s" "$s"
        printf 'Try ezlog%s p2-%s.s o-p2-%s-e.bin\r\n' "$(ezflag p2-$s.s)" "$s" "$s"
    done
    printf 'Echo\r\n'

    printf 'Echo -- 3. the same sizes, every reference settled last --\r\n'
    for s in 32k 64k 128k 256k 512k 1m; do
        printf 'Try zw64k -ez80 p3-%s.s o-p3-%s-z.bin\r\n' "$s" "$s"
        printf 'Try ezlog%s p3-%s.s o-p3-%s-e.bin\r\n' "$(ezflag p3-$s.s)" "$s" "$s"
    done
    printf 'Echo\r\n'
    printf 'Echo === done. Copy kit.log off the card. ===\r\n'
} > "$OUT/kit.obey"

# ----------------------------------------------------------------------
echo "writing README"
cat > "$OUT/README.txt" <<'EOF'
zap output-window hardware kit
==============================

WHAT THIS IS FOR
    The output window writes the binary as it fills and reads chunks of it
    back to patch them. The emulator is exact on CPU, so it can say what the
    assembler costs, but not what the card costs. This measures that on the
    machine.

HOW TO RUN IT
    Copy everything here onto the SD card, including bin/.
    Copy the eight zap binaries and ezlog.bin from bin/ into the card's /bin.
    Then, from the directory holding the sources:

        *Obey -v kit.obey

    -v echoes each line before it runs. Obey needs MOS 3.0 or above.
    It takes a while; part 3 is several megabytes of source to read.

    When it finishes, copy kit.log off the card.

WHAT IS MEASURED
    1. One 96 KiB output -- isa_real's instruction mix -- assembled at every
       window size from 512 bytes to 64 KiB, then by ez80asm. 96 KiB because
       the assembler could manage that before the window existed, so the
       sweep is measuring the window rather than the difference between
       working and not working.

    2. Output size on its own: 32 KiB to 1 MiB of BLKB with no labels and no
       forward references. The floor -- writing the bytes and nothing else.

    3. The same sizes at isa_real's label density (3.7% of lines define one,
       about 18% refer to one) with every definition at the end of the file,
       so every reference is settled after its site has been written out and
       forgotten. The worst shape there is for a window.

    ez80asm is run on all of them for comparison. It holds its output in
    memory, so it is expected to fail somewhere in parts 2 and 3; the script
    uses *Try so that a failure is recorded and the run carries on.

THE LOG
    One line per run, written by the assembler itself, because MOS has no
    output redirection and no program can run another:

        zap w=65536 src=p3-256k.s out=262150 t=12.34
        ez80asm src=p3-256k.s t=41.00

    A run that failed writes no line at all -- its absence is the result.

WHAT THE BINARIES ARE
    bin/zw512 .. bin/zw64k   zap, one per window size, built with -DKITLOG
    bin/ezlog                ez80asm v2.2 as released, plus the same log line

    The log line is the only difference in either case and it runs after the
    clock is read. Neither is a binary anyone ships.

    The outputs are kept per case (o-*.bin) so they can be compared afterwards
    on a PC: every window size in part 1 must produce identical bytes, and
    zap's output must match ez80asm's wherever ez80asm managed to produce one.
EOF

echo
echo "kit in $OUT"
du -sh "$OUT" | sed 's/^/  total /'
ls -l "$OUT"/*.s | awk '{printf "  %-14s %8d\n", $NF, $5}' | sed 's|.*/||'
