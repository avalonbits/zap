#!/bin/bash
# Harvests Agon assembly sources into a corpus for benchmarking and for
# measuring what real label sets look like.
#
#   test/corpus-full/harvest.sh [dir]        default: ~/agon-corpus
#
# NOTHING IS VENDORED. The clones land outside this repository on purpose:
# eighty-nine projects under whatever licences their authors chose is not
# something to copy into zap as a side effect of wanting a benchmark. What is
# committed here is this script and the figures derived from what it fetches,
# so any claim made from the corpus can be reproduced without the corpus being
# in the tree.
#
# The list comes from https://github.com/sabotrax/agon-software, a curated
# index of Agon software. The commit it was read at is recorded in the
# manifest, because that list changes and a corpus that silently grows makes
# every earlier measurement incomparable -- the same reason gen_isa.sh says so.
#
# Shallow clones, so this is bounded by breadth and not by history.
set -uo pipefail

DEST="${1:-$HOME/agon-corpus}"
INDEX_RAW=https://raw.githubusercontent.com/sabotrax/agon-software/main/README.md
INDEX_API=https://api.github.com/repos/sabotrax/agon-software/commits/main

mkdir -p "$DEST"
cd "$DEST"

sha=$(curl -sSL "$INDEX_API" 2>/dev/null \
      | sed -n 's/.*"sha": *"\([0-9a-f]\{40\}\)".*/\1/p' | head -1)
curl -sSL "$INDEX_RAW" -o index.md || { echo "cannot read the index" >&2; exit 2; }

grep -oE 'https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+' index.md \
  | sed -e 's#/tree/.*##' -e 's#\.git$##' | sort -u > repos.txt

{
    echo "# Agon corpus manifest"
    echo "# index: sabotrax/agon-software at ${sha:-unknown}"
    echo "# fetched: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# repo<TAB>status<TAB>licence<TAB>asm-files<TAB>asm-lines"
} > manifest.tsv

n=0
while IFS= read -r url; do
    name=$(printf '%s' "$url" | sed -e 's#https://github.com/##' -e 's#/#-#g')
    n=$((n + 1))
    if [ ! -d "$name/.git" ]; then
        rm -rf "$name"
        timeout 180 git clone --depth 1 --quiet "$url" "$name" 2>/dev/null
    fi
    if [ ! -d "$name" ]; then
        printf '%s\tunreachable\t-\t0\t0\n' "$url" >> manifest.tsv
        continue
    fi
    lic=$(ls "$name" 2>/dev/null | grep -iE '^(licen[cs]e|copying)' | head -1)
    lic="${lic:-none}"
    files=$(find "$name" -type f \( -iname '*.s' -o -iname '*.asm' -o -iname '*.inc' \) 2>/dev/null | wc -l)
    lines=$(find "$name" -type f \( -iname '*.s' -o -iname '*.asm' -o -iname '*.inc' \) -exec cat {} + 2>/dev/null | wc -l)
    printf '%s\tok\t%s\t%s\t%s\n' "$url" "$lic" "$files" "$lines" >> manifest.tsv
done < repos.txt

echo "harvested $n repositories into $DEST"
awk -F'\t' '!/^#/ && $2=="ok" {f+=$4; l+=$5; r++} END {printf "  %d cloned, %d assembly files, %d lines\n", r, f, l}' manifest.tsv
