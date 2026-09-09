#!/bin/bash
# Times both assemblers over the corpus ON THE AGON, source by source, and
# reports the speedup for each and the geometric mean of them.
#
#   test/bench/corpus-target.sh [outdir]
#
# The host cannot answer this question. test/bench/corpus-time.sh asks it there
# and gets 1.36x; the same sources here are between five and twenty times.
# .internal/host-profile-does-not-predict-target has the reason and this is the
# largest example of it the repository has.
#
# HOW THE TIME IS TAKEN. Each assembler's own `Done in` line, which is what
# every other measurement in this directory uses, and the emulator runs
# without -u for the same reason as ever. That clock reads hundredths, and a
# corpus source is small enough that one run of zap lands on 0.02 or 0.03 --
# so every source is assembled **several times and the reported times are
# summed**. The tick boundary falls in a different place on each run, so the
# quantisation averages out rather than accumulating: eight runs of a 25 ms
# assembly sum to 0.20 give or take a hundredth, which is 5% rather than 50%.
#
# zap is run more times than ez80asm because it is the faster of the two and
# has the coarser measurement to fight -- twelve runs against six for a source
# under four kilobytes. Big sources get fewer of both: BBC BASIC is 22 seconds
# a run there and the point is diminishing.
#
# A source that still reads zero on both after all that is one neither
# assembler spends a tick on. It is counted and set aside rather than turned
# into a ratio, because a ratio of two numbers below the clock measures
# nothing.
#
# ONE EMULATOR BOOT PER DIRECTORY rather than one for the whole corpus. A boot
# costs about eight seconds and a hang costs the timeout, so this trades a few
# minutes for not losing the run to one bad source. Each boot assembles every
# comparable source in one directory.
#
# WHAT IS TIMED. Only the sources both assemblers accept. A source one of them
# refuses stops early, and timing a diagnostic against an assembly is not a
# speedup. The corpus runner says which those are; this script asks it.
set -uo pipefail

cd "$(dirname "$0")/../.."

OUTDIR="${1:-$(mktemp -d)}"
mkdir -p "$OUTDIR"
EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"
CORPUS="test/corpus"
REGRESS="test/regress"

case "$(uname -m)" in
    x86_64|amd64)   HOSTEZ="test/ref/linux_x86_64/ez80asm" ;;
    aarch64|arm64)  HOSTEZ="test/ref/linux_aarch64/ez80asm" ;;
    *) echo "no vendored ez80asm for $(uname -m)" >&2; exit 2 ;;
esac
HOSTEZ=$(cd "$(dirname "$HOSTEZ")" && pwd)/$(basename "$HOSTEZ")

[ -x "$EMU/agon-cli-emulator" ] || { echo "no emulator at $EMU" >&2; exit 2; }
[ -f bin/zap.bin ] || { echo "build bin/zap.bin first (make)" >&2; exit 2; }
[ -f test/ref/agon/ez80asm.bin ] || { echo "no Agon ez80asm" >&2; exit 2; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# The host build, used only to find out which sources both assemblers accept.
cc -std=gnu11 -O1 -fsigned-char \
   -include test/stubs/host_types.h -Isrc -Itest/stubs \
   -o "$TMP/zap" src/*.c test/stubs/agon_stubs.c || exit 1

echo "finding the sources both assemblers accept..."
: > "$OUTDIR/accepted"
for dir in "$CORPUS"/*/ "$REGRESS"/*/; do
    [ -d "$dir/tests" ] || continue
    name=$(basename "$dir")
    rm -rf "$TMP/w"; mkdir -p "$TMP/w"
    cp -r "$dir"/tests/* "$TMP/w/" 2>/dev/null
    for src in "$dir"/tests/*.s; do
        [ -f "$src" ] || continue
        base=$(basename "$src" .s)
        (cd "$TMP/w" && rm -f a.bin b.bin \
            && "$TMP/zap" -ez80 "$base.s" a.bin > /dev/null 2>&1
           "$HOSTEZ" "$base.s" b.bin -c > /dev/null 2>&1)
        if [ -f "$TMP/w/a.bin" ] && [ -f "$TMP/w/b.bin" ]; then
            printf '%s %s %d\n' "$name" "$base" "$(stat -c%s "$src")" \
                >> "$OUTDIR/accepted"
        fi
    done
done
echo "$(wc -l < "$OUTDIR/accepted") sources to time"

: > "$OUTDIR/times"
g=0
for dir in "$CORPUS"/*/ "$REGRESS"/*/; do
    [ -d "$dir/tests" ] || continue
    name=$(basename "$dir")
    grep -q "^$name " "$OUTDIR/accepted" || continue
    g=$((g + 1))

    sd="$TMP/sd"
    rm -rf "$sd"; mkdir -p "$sd/bin" "$sd/g"
    cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
    cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
    cp bin/zap.bin "$sd/bin/zap.bin"
    cp test/ref/agon/ez80asm.bin "$sd/bin/ez80asm.bin"
    cp -r "$dir"/tests/* "$sd/g/" 2>/dev/null

    # A short name for the directory on the card, because the interesting part
    # of a corpus path is the file and MOS has to type the rest.
    # By the whole tree and not by the file named on the command line. BBC
    # BASIC's entry source is 554 bytes and its program is 386 KB; sized by
    # the entry it drew twelve runs of a four-second assembly and six of a
    # twenty-two second one.
    tree=$(cat "$dir"/tests/* 2>/dev/null | wc -c)

    # `-m` is the reference's minimum-memory mode, and BBC BASIC does not
    # assemble there without it: ez80asm's ordinary buffers do not fit beside
    # a 386 KB program in 512 KB, and what happens is not an error message --
    # the machine resets, MOS runs autoexec again, and the group assembles
    # itself forever. It costs the reference a little time (rokky is 2.70s
    # with it against 2.50s without), so it is given only where it is needed,
    # which is what bench.sh does too.
    mflag=""
    [ "$tree" -gt 100000 ] && mflag=" -m"

    {
        printf 'cd /g\r\n'
        while read -r dname base bytes; do
            [ "$dname" = "$name" ] || continue
            # Runs, by how much work there is. A source of a few hundred bytes
            # needs the repetition; BBC BASIC does not, and would cost four
            # minutes to get it.
            if [ "$tree" -gt 20000 ]; then
                nz=2; ne=1
            elif [ "$tree" -gt 4000 ]; then
                nz=6; ne=3
            else
                nz=12; ne=6
            fi
            for ((i = 0; i < nz; i++)); do
                printf 'zap %s.s o.bin -ez80\r\n' "$base"
            done
            for ((i = 0; i < ne; i++)); do
                printf 'ez80asm %s.s o.bin -c%s\r\n' "$base" "$mflag"
            done
        done < "$OUTDIR/accepted"
        # One more assembly, and its timing is thrown away.
        #
        # The emulator stops the moment the guest writes the exit port, and
        # MOS is still flushing the line before it: the last source in the
        # group came out as "Done in " with the number missing, which for a
        # source run once -- a whole program -- is the whole measurement. So
        # something cheap goes last and loses its line instead. bench.sh does
        # the same thing for the same reason.
        printf 'zap zzflush.s zzflush.bin -ez80\r\n'
        printf 'emulator_exit_success\r\n'
    } > "$sd/autoexec.txt"
    printf '  nop\n  ret\n' > "$sd/g/zzflush.s"

    n=$(grep -c "^$name " "$OUTDIR/accepted")
    echo "[$g] $name: $n sources, tree $((tree / 1024)) KB$([ -n "$mflag" ] && echo ', ez80asm -m')"

    fifo="$TMP/f"; rm -f "$fifo"; mkfifo "$fifo"
    tail -f /dev/null > "$fifo" &
    hold=$!
    # A timeout with a bound rather than an hour: a source that resets the
    # machine restarts autoexec from the top and would otherwise assemble
    # itself until the timeout ran out.
    (cd "$EMU" && timeout 900 ./agon-cli-emulator --sdcard "$sd" -z \
        < "$fifo" > "$TMP/cap" 2>&1)
    kill "$hold" 2>/dev/null; wait "$hold" 2>/dev/null

    # MOS says its version once per boot. Twice means something reset the
    # machine, and every timing in the group after that point is of a rerun.
    boots=$(grep -c "MOS Version" "$TMP/cap" || true)
    if [ "${boots:-1}" -gt 1 ]; then
        echo "    WARNING: the machine reset $((boots - 1)) time(s) in this group"
    fi

    # Each run prints "Assembling <file>", then a line that says which
    # assembler it was -- zap writes the output and says so, ez80asm counts
    # its passes -- and then "Done in X.XX seconds". Summed per source per
    # assembler, which is the whole point of running them more than once.
    tr -d '\r' < "$TMP/cap" | awk -v group="$name" '
        /^Assembling / { file = $2; sub(/\.s$/, "", file); who = ""; next }
        /^Wrote /      { who = "zap"; next }
        /^Pass 1/      { who = "ez"; next }
        /^Done in /    {
            if (file == "" || who == "" || file == "zzflush") next
            t = $3 + 0
            if (who == "zap") { zt[file] += t; zn[file]++ }
            else              { et[file] += t; en[file]++ }
            next
        }
        END {
            for (f in zt) {
                if (en[f] > 0 && zn[f] > 0) {
                    printf "%s %s %.4f %.4f %d %d\n", group, f,
                           zt[f] / zn[f], et[f] / en[f], zn[f], en[f]
                }
            }
        }' >> "$OUTDIR/times"
    cp "$TMP/cap" "$OUTDIR/cap.$name.txt"
done

echo
below=$(awk '$3 == 0 || $4 == 0' "$OUTDIR/times" | wc -l)
echo "$below of $(wc -l < "$OUTDIR/times") sources read zero on one side or the"
echo "other even after the repeats, and are left out of everything below."
echo
sort -k4 -n -r "$OUTDIR/times" | head -15 | awk '
    BEGIN { printf "%-16s %-30s %8s %8s %8s\n", "group", "source", "zap s", "ez s", "speedup" }
    $3 > 0 && $4 > 0 { printf "%-16s %-30s %8.3f %8.3f %7.1fx\n", $1, $2, $3, $4, $4 / $3 }'
echo
awk '$3 > 0 && $4 > 0 { printf "%.3f %s/%s\n", $4 / $3, $1, $2 }' "$OUTDIR/times" \
    | sort -n | head -6 | awk '
    BEGIN { printf "%-48s %8s\n", "least sped up", "speedup" }
    { printf "%-48s %7.1fx\n", $2, $1 }'
echo
awk '
    $3 > 0 && $4 > 0 { s += log($4 / $3); n++; zt += $3; et += $4
             r = $4 / $3
             if (n == 1 || r < lo) lo = r
             if (n == 1 || r > hi) hi = r }
    END {
        printf "GEOMETRIC MEAN SPEEDUP   %.2fx   over %d sources\n", exp(s / n), n
        printf "range                    %.1fx to %.1fx\n", lo, hi
        printf "summed                   zap %.2f s, ez80asm %.2f s  (%.2fx)\n",
               zt, et, et / zt
    }' "$OUTDIR/times"
echo
echo "per-source times in $OUTDIR/times, guest output in $OUTDIR/cap.*.txt"
