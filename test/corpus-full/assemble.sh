#!/bin/bash
# Runs the reference assembler over the full corpus and reports what it makes
# of it. Step one of the note in .internal/performance-notes.md: anything
# ez80asm rejects is out of scope, because zap is not trying to be better than
# the reference, it is trying to agree with it.
#
#   test/corpus-full/assemble.sh [dir]        default: ~/agon-corpus
#
# ENTRY POINTS, NOT EVERY FILE. Most of the corpus is includes and fragments
# that were never meant to assemble alone, and running the assembler over them
# would produce a pile of failures that say nothing. A file is an entry point
# when nothing else in its project includes it -- ez80asm takes `include`,
# `INCLUDE` and `.include`, all three of which the corpus uses -- and it is
# assembled from its own directory, because include paths are relative to it.
#
# The output is a TSV of one row per entry point, so a later run can be
# diffed against an earlier one rather than re-argued.
set -uo pipefail

DEST="${1:-$HOME/agon-corpus}"
REF="$(cd "$(dirname "$0")/../.." && pwd)/test/ref/linux_x86_64/ez80asm"
[ -x "$REF" ] || { echo "no vendored ez80asm at $REF" >&2; exit 2; }
cd "$DEST" || exit 2

OUT=assemble.tsv
printf '# repo\tfile\tresult\tdetail\n' > "$OUT"

# z88dk is not Agon code, and the reference's own suite is already vendored in
# test/corpus -- its error tests exist to be rejected, so counting them as
# failures says nothing.
for d in */; do
    case "$d" in z88dk-z88dk/|envenomator-agon-ez80asm/) continue;; esac
    [ -d "$d" ] || continue

    mapfile -t files < <(find "$d" -type f \( -iname '*.s' -o -iname '*.asm' \) 2>/dev/null)
    [ "${#files[@]}" -eq 0 ] && continue

    # Everything any file in this project includes, by basename.
    included=$(find "$d" -type f \( -iname '*.s' -o -iname '*.asm' -o -iname '*.inc' \) -exec \
        grep -hoiE '^[[:space:]]*(\.?include)[[:space:]]+"[^"]+"' {} + 2>/dev/null \
        | sed -E 's/.*"([^"]+)"/\1/' | sed 's#.*/##' | tr 'A-Z' 'a-z' | sort -u)

    for f in "${files[@]}"; do
        base=$(basename "$f" | tr 'A-Z' 'a-z')
        case $'\n'"$included"$'\n' in *$'\n'"$base"$'\n'*) continue;; esac

        dir=$(dirname "$f")
        name=$(basename "$f")
        # The reference colours its diagnostics; strip that, or the TSV
        # cannot be diffed or grouped.
        err=$( cd "$dir" && timeout 60 "$REF" "$name" /tmp/corpus_out.bin 2>&1 \
               | tr -d '\r' | sed -E 's/\x1b?\[[0-9]*m//g' | tr '\n' ' ' )
        rc=$?
        if [ -s /tmp/corpus_out.bin ] && printf '%s' "$err" | grep -q 'Done in'; then
            sz=$(stat -c%s /tmp/corpus_out.bin 2>/dev/null || echo 0)
            printf '%s\t%s\tok\t%s bytes\n' "${d%/}" "$f" "$sz" >> "$OUT"
        else
            detail=$(printf '%s' "$err" | grep -oE '[A-Za-z_./-]+\.[a-zA-Z]+ line [0-9]+: .*' | head -1)
            [ -z "$detail" ] && detail=$(printf '%s' "$err" | head -c 120)
            [ -z "$detail" ] && detail="no output (rc=$rc)"
            printf '%s\t%s\tfail\t%s\n' "${d%/}" "$f" "$detail" >> "$OUT"
        fi
        rm -f /tmp/corpus_out.bin
    done
done

ok=$(awk -F'\t' '!/^#/ && $3=="ok"' "$OUT" | wc -l)
bad=$(awk -F'\t' '!/^#/ && $3=="fail"' "$OUT" | wc -l)
printf '%d entry points: %d assemble, %d do not (%.0f%%)\n' \
    "$((ok+bad))" "$ok" "$bad" "$(awk -v o=$ok -v b=$bad 'BEGIN{print 100*o/(o+b)}')"
