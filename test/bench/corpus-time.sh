#!/bin/bash
# Times both assemblers over every source in the corpus, one at a time, and
# reports the speedup per source and the geometric mean of them.
#
#   test/bench/corpus-time.sh [runs]        default 25 runs per source
#
# THIS IS A HOST MEASUREMENT AND THE HOST IS NOT THE AGON. Everything else in
# test/bench runs on fab-agon-emulator because that is the machine zap is for,
# and the optimization guide (section 5) says why: a change that
# read 0.71x on the host read 0.98x on the Agon. A per-source figure cannot be
# taken there at all -- the emulator boots MOS for each run, and every corpus
# source is small enough that the assembler's own clock reads 0.00 seconds. So
# this measures the two programs as they run here, and the number to quote for
# the Agon is still bench.sh's.
#
# **Process startup is subtracted.** A corpus source is twenty lines; starting
# a process costs more than assembling it, and the two binaries do not cost the
# same to start -- so a raw wall-clock ratio would mostly be reporting the size
# of ez80asm's dynamic linking. Each binary is timed on an empty source first
# and that baseline comes off every measurement. What is left is the work.
#
# Sources both assemblers accept are what the geometric mean is taken over. A
# source one of them refuses stops early and would be timing a diagnostic
# rather than an assembly; those are counted and set aside. The geometric mean
# rather than the arithmetic one because these are ratios: the mean of 2x and
# 0.5x has to be 1x.
set -uo pipefail

cd "$(dirname "$0")/../.."

RUNS="${1:-25}"
CORPUS="test/corpus"
REGRESS="test/regress"

case "$(uname -m)" in
    x86_64|amd64)   EZ="test/ref/linux_x86_64/ez80asm" ;;
    aarch64|arm64)  EZ="test/ref/linux_aarch64/ez80asm" ;;
    *) echo "no vendored ez80asm for $(uname -m)" >&2; exit 2 ;;
esac
EZ=$(cd "$(dirname "$EZ")" && pwd)/$(basename "$EZ")

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# -O2, which is what a release build of either of these would be. The corpus
# runner builds -O1 because it is checking bytes and not time.
cc -std=gnu11 -O2 -fsigned-char \
   -include test/stubs/host_types.h -Isrc -Itest/stubs \
   -o "$OUT/zap" src/*.c test/stubs/agon_stubs.c || exit 1
ZAP="$OUT/zap"

# Nanoseconds for one run of a command, as the mean of `n` runs timed together
# -- one timing call rather than n -- and then the **best of three** of those.
#
# The minimum and not the mean of the three. Everything that goes wrong during
# a measurement on a shared machine makes it slower: a scheduler decision, a
# page fault, another process. The fastest run is the one with the least of
# that in it, and it is the only estimator here that does not drift with what
# else the machine is doing.
timeit() {
    local n="$1"; shift
    local t0 t1 i r best=0 this
    for r in 1 2 3; do
        t0=$(date +%s%N)
        for ((i = 0; i < n; i++)); do
            "$@" > /dev/null 2>&1
        done
        t1=$(date +%s%N)
        this=$(( (t1 - t0) / n ))
        if [ "$best" = 0 ] || [ "$this" -lt "$best" ]; then
            best=$this
        fi
    done
    echo "$best"
}

# What each binary costs before it has assembled anything.
mkdir -p "$OUT/base"
: > "$OUT/base/empty.s"
(cd "$OUT/base" && :)
zbase=$(cd "$OUT/base" && timeit $((RUNS * 2)) "$ZAP" -ez80 empty.s empty.bin)
ebase=$(cd "$OUT/base" && timeit $((RUNS * 2)) "$EZ" empty.s empty.bin -c)
echo "startup, subtracted from every measurement below:"
echo "  zap      $((zbase / 1000)) us"
echo "  ez80asm  $((ebase / 1000)) us"
echo

rows="$OUT/rows"
: > "$rows"
both=0; refused=0; tiny=0

for dir in "$CORPUS"/*/ "$REGRESS"/*/; do
    [ -d "$dir/tests" ] || continue
    name=$(basename "$dir")

    # Staged once per directory, not once per source: an INCLUDE resolves
    # against the working directory and the copy is not what is being timed.
    rm -rf "$OUT/w"; mkdir -p "$OUT/w"
    cp -r "$dir"/tests/* "$OUT/w/" 2>/dev/null

    for src in "$dir"/tests/*.s; do
        [ -f "$src" ] || continue
        base=$(basename "$src" .s)

        # Both have to accept it, or the timing is of a diagnostic.
        (cd "$OUT/w" && rm -f "$base.zbin" "$base.ebin" \
            && "$ZAP" -ez80 "$base.s" "$base.zbin" > /dev/null 2>&1
           "$EZ" "$base.s" "$base.ebin" -c > /dev/null 2>&1)
        if [ ! -f "$OUT/w/$base.zbin" ] || [ ! -f "$OUT/w/$base.ebin" ]; then
            refused=$((refused + 1))
            continue
        fi

        zt=$(cd "$OUT/w" && timeit "$RUNS" "$ZAP" -ez80 "$base.s" "$base.zbin")
        et=$(cd "$OUT/w" && timeit "$RUNS" "$EZ" "$base.s" "$base.ebin" -c)

        zw=$((zt - zbase))
        ew=$((et - ebase))
        # A source whose work does not clear the noise floor says nothing about
        # either assembler, and a ratio taken from two such numbers says less.
        # 50 us is about five times what the best-of-three leaves behind here.
        if [ "$zw" -lt 50000 ] || [ "$ew" -lt 50000 ]; then
            tiny=$((tiny + 1))
            continue
        fi
        both=$((both + 1))
        bytes=$(stat -c%s "$OUT/w/$base.zbin")
        printf '%s/%s %d %d %d %d %d\n' \
            "$name" "$base" "$zw" "$ew" "$bytes" "$zt" "$et" >> "$rows"
    done
done

echo "$both sources timed"
echo "  $refused set aside: one of the two refuses them, so the timing would be"
echo "             of a diagnostic and not of an assembly"
echo "  $tiny set aside: the work does not clear 50 us, which is the floor a"
echo "             best-of-three leaves on this machine"
echo

sort -k3 -n -r "$rows" | head -12 | awk '
    BEGIN { printf "%-44s %8s %8s %8s\n", "most work to assemble", "zap us", "ez us", "speedup" }
    { printf "%-44s %8.0f %8.0f %7.2fx\n", $1, $2/1000, $3/1000, $3/$2 }'
echo
awk '{ printf "%.4f %s\n", $3/$2, $1 }' "$rows" | sort -n | head -6 | awk '
    BEGIN { printf "%-44s %8s\n", "least sped up", "speedup" }
    { printf "%-44s %7.2fx\n", $2, $1 }'
echo
awk '{ printf "%.4f %s\n", $3/$2, $1 }' "$rows" | sort -n -r | head -6 | awk '
    BEGIN { printf "%-44s %8s\n", "most sped up", "speedup" }
    { printf "%-44s %7.2fx\n", $2, $1 }'
echo
awk '
    { w += log($3 / $2); r += log($6 / $5); n++;
      if ($3/$2 < lo || n == 1) lo = $3/$2
      if ($3/$2 > hi || n == 1) hi = $3/$2
      zt += $2; et += $3 }
    END {
        printf "GEOMETRIC MEAN SPEEDUP\n"
        printf "  assembly only          %.2fx   over %d sources\n", exp(w / n), n
        printf "  whole invocation       %.2fx   startup included, as a shell sees it\n", exp(r / n)
        printf "\n"
        printf "range, assembly only     %.2fx to %.2fx\n", lo, hi
        printf "summed assembly work     zap %.1f ms, ez80asm %.1f ms  (%.2fx)\n",
               zt / 1000000, et / 1000000, et / zt
    }' "$rows"
