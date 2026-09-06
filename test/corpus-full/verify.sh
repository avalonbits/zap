#!/bin/bash
# Step two: does zap agree with ez80asm, byte for byte, on the corpus?
#
#   test/corpus-full/verify.sh [dir]        default: ~/agon-corpus
#
# Only the entry points assemble.sh marked ok are tried. The rest are ZDS
# dialect and out of scope -- see the README. Expect failures here; that is the
# point of running it.
#
# Both assemblers are run from the file's own directory, because include paths
# are relative to it and ez80asm has a filename length limit that long absolute
# paths trip.
set -uo pipefail

DEST="${1:-$HOME/agon-corpus}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REF="$ROOT/test/ref/linux_x86_64/ez80asm"
ZAP="${ZAP_HOST:-}"

if [ -z "$ZAP" ]; then
    ZAP=$(mktemp -u)
    # shellcheck disable=SC2046
    cc -std=gnu11 -O1 -fsigned-char -include "$ROOT/test/stubs/host_types.h" \
       -I"$ROOT/src" -I"$ROOT/test/stubs" -o "$ZAP" "$ROOT/src/main.c" \
       $(sed -n '/^SRCS=(/,/)/p' "$ROOT/test/run.sh" | tr -d '()' | sed 's/SRCS=//' \
         | while read -r l; do for f in $l; do printf '%s ' "$ROOT/$f"; done; done) \
       2>/dev/null || { echo "cannot build zap for the host" >&2; exit 2; }
fi
[ -x "$REF" ] || { echo "no vendored ez80asm at $REF" >&2; exit 2; }

cd "$DEST" || exit 2
[ -f assemble.tsv ] || { echo "run assemble.sh first" >&2; exit 2; }

OUT=verify.tsv
printf '# repo\tfile\tresult\tdetail\n' > "$OUT"

while IFS=$'\t' read -r repo file result _; do
    case "$repo" in '#'*|envenomator-agon-ez80asm) continue;; esac
    [ "$result" = ok ] || continue

    dir=$(dirname "$file"); name=$(basename "$file")
    r=$(mktemp); z=$(mktemp)
    ( cd "$dir" && "$REF" "$name" "$r" ) >/dev/null 2>&1
    zerr=$( cd "$dir" && timeout 120 "$ZAP" "$name" "$z" 2>&1 | tr -d '\r' | tr '\n' ' ' )

    if [ ! -s "$z" ]; then
        d=$(printf '%s' "$zerr" | grep -oE '[^ ]+ line [0-9]+: .*' | head -1)
        printf '%s\t%s\tzap-failed\t%s\n' "$repo" "$file" "${d:-${zerr:0:100}}" >> "$OUT"
    elif cmp -s "$r" "$z"; then
        printf '%s\t%s\tagree\t%s bytes\n' "$repo" "$file" "$(stat -c%s "$r")" >> "$OUT"
    else
        off=$(cmp "$r" "$z" 2>/dev/null | grep -oE 'byte [0-9]+' | head -1)
        printf '%s\t%s\tdiffer\tref %s / zap %s, first %s\n' "$repo" "$file" \
               "$(stat -c%s "$r")" "$(stat -c%s "$z")" "${off:-?}" >> "$OUT"
    fi
    rm -f "$r" "$z"
done < assemble.tsv

a=$(awk -F'\t' '$3=="agree"' "$OUT" | wc -l)
d=$(awk -F'\t' '$3=="differ"' "$OUT" | wc -l)
f=$(awk -F'\t' '$3=="zap-failed"' "$OUT" | wc -l)
printf '%d tried: %d agree, %d differ, %d zap could not assemble\n' "$((a+d+f))" "$a" "$d" "$f"
