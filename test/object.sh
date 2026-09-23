#!/bin/bash
# Relocatable objects: what `-f elf` writes, checked against binutils.
#
# zap's flat output is already checked byte for byte against ez80asm, so an
# object is checked against that: linked by agondev's ld at an address, it has
# to be exactly what zap writes for the same source as a flat binary at that
# ORG. Two addresses, so a value that happened to be right at one cannot hide.
# Every object must also be read by readelf without a word of complaint.
#
# Usage: test/object.sh [zap]. test/run.sh passes the zap it has just built;
# on its own this builds one.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

ZAP="${1:-}"
if [ -z "$ZAP" ]; then
    ZAP="$OUT/zap"
    cc -std=gnu11 -Wall -Wextra -fsigned-char -g -fsanitize=address,undefined \
        -include "$ROOT/test/stubs/host_types.h" -Isrc -I"$ROOT/test/stubs" \
        -o "$ZAP" src/zap.c src/symtab.c src/scan.c src/expr.c src/macro.c \
        src/directive.c src/insn.c src/object.c src/buf_reader.c src/value.c \
        src/conv.c src/isa_table.c "$ROOT/test/stubs/agon_stubs.c"
fi

BIN="${AGONDEV:-$HOME/agondev}/bin/ez80-none-elf"
if [ ! -x "$BIN-ld" ]; then
    echo "SKIP  no agondev binutils; objects are not checked"
    exit 0
fi

status=0
check() {
    if [ "$2" = "$3" ]; then
        echo "PASS  $1"
    else
        echo "FAIL  $1: got '$2', want '$3'"
        status=1
    fi
}

# Assembles $1 (a name in $OUT) as an object, and says why not if it fails.
obj() {
    "$ZAP" -c -f elf "$OUT/$1.s" "$OUT/$1.o" > "$OUT/$1.log" 2>&1
}

# readelf reads the whole file and says nothing on stderr.
readelf_quiet() {
    local err
    err=$("$BIN-readelf" -a -W "$OUT/$1.o" 2>&1 > /dev/null)
    check "readelf reads $1 without complaint" "$err" ""
}

# One section's bytes, as hex.
section() {
    "$BIN-objcopy" -O binary --only-section="$2" "$OUT/$1.o" "$OUT/$1.sec" 2>/dev/null
    xxd -p "$OUT/$1.sec" | tr -d '\n'
}

# A field of a section header, by column of readelf -S -W.
shfield() {
    "$BIN-readelf" -S -W "$OUT/$1.o" | sed 's/\[ */[/' \
        | awk -v s="$2" -v c="$3" '$2 == s { print $c }'
}

# Linked at $2, the object is what zap writes flat at that ORG.
linked_same() {
    local name="$1" at="$2"
    "$ZAP" -c "$OUT/$name.s" "$OUT/$name.flat" -o "$at" > /dev/null 2>&1
    "$BIN-ld" -e 0 -Ttext="0x$at" --oformat binary -o "$OUT/$name.lnk" \
        "$OUT/$name.o" > /dev/null 2>&1
    if cmp -s "$OUT/$name.lnk" "$OUT/$name.flat"; then
        echo "PASS  $name linked at $at is its flat assembly at $at"
    else
        echo "FAIL  $name linked at $at differs from flat:"
        echo "      $(xxd -p "$OUT/$name.lnk" | head -c 80)"
        echo "      $(xxd -p "$OUT/$name.flat" | head -c 80)"
        status=1
    fi
}

# ----------------------------------------------------------------------
# Code with no relocations: jumps within the segment, `$`, anonymous labels,
# locals, label differences, and a segment larger than the first buffer, so
# fixups land in memory that has been grown since they were made.
# ----------------------------------------------------------------------
{
    printf 'start:  ld a, 1\n'
    printf '@@:     djnz @b\n'
    printf '        jr nz, done\n'
    printf '        jr $\n'
    printf '        jr $+2\n'
    printf '        ld hl, done - start\n'
    printf '        dl end - start, -(start - end)\n'
    printf '        dw done - start\n'
    printf '        db $ - start\n'
    printf '        dl @b - start\n'
    printf '@loop:  jr @loop\n'
    printf '        ld de, end - @loop\n'
    printf '        jr @f\n'
    printf '        nop\n'
    printf '@@:     ret\n'
    printf 'done:   ret\n'
    for i in $(seq 1 1500); do printf '        ld bc, far - done\n'; done
    printf 'far:    jr far\n'
    printf 'end:    nop\n'
} > "$OUT/code.s"
if obj code; then
    echo "PASS  code assembles as an object"
    readelf_quiet code
    linked_same code 50000
    linked_same code 7A1234
    check "the default output of -f is <source>.o" \
        "$(cd "$OUT" && "$ZAP" -c -f elf code.s > /dev/null 2>&1; ls "$OUT/code.o" > /dev/null && echo yes)" yes
else
    echo "FAIL  code: $(tr -d '\r' < "$OUT/code.log")"
    status=1
fi

# ----------------------------------------------------------------------
# Segments: each one's bytes, sizes and alignment, with segments left and
# returned to, and both spellings.
# ----------------------------------------------------------------------
cat > "$OUT/segs.s" <<'EOF'
        nop
        SEGMENT DATA
tbl:    db 1, 2, 3
        ds 2
        .rodata
msg:    db "hi", 0
        align 4
        SEGMENT BSS
buf:    ds 10
        align 8
cnt:    ds 3
        .text
        ld hl, tbl2 - tbl3
        .section .data
tbl3:   db 4
tbl2:   blkb 2, fillv
        segment code
        ret
fillv:  equ 0x5A
        .data
        ds 1
        .text
        nop
EOF
if obj segs; then
    echo "PASS  segs assembles as an object"
    readelf_quiet segs
    check ".text holds both code runs" "$(section segs .text)" "0021010000c900"
    check ".data keeps a DS at the end of a run, and a forward BLK fill" \
        "$(section segs .data)" "010203ffff045a5aff"
    check ".rodata writes a trailing ALIGN" "$(section segs .rodata)" "686900ff"
    check ".bss is the reservations and nothing else" "$(shfield segs .bss 3)" NOBITS
    check ".bss size is 10, aligned to 8, then 3" "$(shfield segs .bss 6)" 000013
    check ".bss is aligned to its largest ALIGN" "$(shfield segs .bss 11)" 8
    check ".rodata is aligned to its largest ALIGN" "$(shfield segs .rodata 11)" 4
    check ".text with no ALIGN is aligned to 1" "$(shfield segs .text 11)" 1
    check "the object is EZ80 and ADL" \
        "$("$BIN-readelf" -h "$OUT/segs.o" | awk '/Flags:/ { print $2 }')" "0x84,"
    "$BIN-ld" --no-warn-rwx-segments -e 0 -Ttext=0x40000 -o "$OUT/segs.elf" "$OUT/segs.o" > "$OUT/ld.log" 2>&1
    check "ld links it without a word" "$(cat "$OUT/ld.log")" ""
else
    echo "FAIL  segs: $(tr -d '\r' < "$OUT/segs.log")"
    status=1
fi

# A reservation at the very end of the object is written too: it is the
# space of a variable, not the end of a file.
printf '        nop\n        ds 2\n' > "$OUT/tail.s"
if obj tail; then
    check "a DS at the end of the last segment is written" "$(section tail .text)" "00ffff"
else
    echo "FAIL  tail: $(tr -d '\r' < "$OUT/tail.log")"
    status=1
fi

# ----------------------------------------------------------------------
# Relocations: what is written for each kind of reference, and the symbols.
# A label defined here is relocated against its section, as GNU as does it;
# only an import is named. An import nothing uses is left out.
# ----------------------------------------------------------------------
cat > "$OUT/rel.s" <<'EOF'
        XREF    ext, unused:ROM
        XDEF    g, tbl, absv
g:      call ext
        ld hl, ext+5
        ld hl, tbl+2
@l:     jp @l
        ld hl, $
        SEGMENT DATA
tbl:    dl g, fwd, -1
        .text
fwd:    ret
absv:   equ 5
EOF
if obj rel; then
    readelf_quiet rel
    got=$("$BIN-readelf" -r -W "$OUT/rel.o" | awk '/R_Z80/ { print $1, $3, $5, $6, $7 }' | tr '\n' ';')
    check "each reference is a 24-bit relocation against the right base" "$got" \
"00000001 R_Z80_24 ext + 0;00000005 R_Z80_24 ext + 5;00000009 R_Z80_24 .data + 2;0000000d R_Z80_24 .text + c;00000011 R_Z80_24 .text + 10;00000000 R_Z80_24 .text + 0;00000003 R_Z80_24 .text + 14;"
    got=$("$BIN-readelf" -s -W "$OUT/rel.o" | awk '$5 == "GLOBAL" { print $2, $7, $8 }' | tr '\n' ';')
    check "exports and used imports are global, sorted by name" "$got" \
        "00000005 ABS absv;00000000 UND ext;00000000 1 g;00000000 2 tbl;"
    check "every relocated field holds zeros" "$(section rel .text)" \
        "cd0000002100000021000000c300000021000000c9"
else
    echo "FAIL  rel: $(tr -d '\r' < "$OUT/rel.log")"
    status=1
fi

# Every kind of relocation, linked against a second object that defines the
# imports, and compared at two addresses with the flat assembly of the two
# sources in the same order: relative jumps to an import, 16-bit and 8-bit
# fields, each byte of an address, distances and `$` in expressions that need
# a number, and a local label in an expression settled on its own line.
cat > "$OUT/kinds_x.s" <<'EOF'
        XDEF    ext, tbl
ext:    nop
        nop
        nop
tbl:    dl 0x123456
EOF
cat > "$OUT/kinds.s" <<'EOF'
        XREF    ext, tbl
start:  jr ext
        djnz ext+2
        dw start, ext, fin - start
        db start, ext
        ld a, start >> 8
        ld a, (ext + 3) >> 16
        ld a, start & 0xFF
        ld a, (start >> 8) & 0xFF
        ld hl, (fin - start) / 2
        ld a, fin - start & 0xFF
        ld bc, fin - start
        ld.sis hl, start + 1
msg:    db "hello"
len:    equ $ - msg
        ds len
        if len == 5
        db 1
        endif
        ld hl, (@f - msg) * 3
@@:     ld de, ($ - start) << 1
later:  jr @b
fin:    ret
@t:     ld a, @t >> 8
        db tbl >> 8, (tbl + 1) >> 16, later & 0xff, fin >> 16
        dw (fin - msg) >> 1
EOF
grep -v XDEF "$OUT/kinds_x.s" > "$OUT/kinds_flat.s"
grep -v XREF "$OUT/kinds.s" >> "$OUT/kinds_flat.s"
if obj kinds && obj kinds_x; then
    readelf_quiet kinds
    got=$("$BIN-readelf" -r -W "$OUT/kinds.o" | awk '/R_Z80/ { print $3 }' | sort | uniq -c \
          | awk '{ print $2 "=" $1 }' | tr '\n' ' ')
    check "each kind of relocation is written" "$got" \
        "R_Z80_8_PCREL=2 R_Z80_BYTE0=4 R_Z80_BYTE1=4 R_Z80_BYTE2=3 R_Z80_WORD0=3 "
    got=$("$BIN-readelf" -r -W "$OUT/kinds.o" | awk '$1 == "00000001" || $1 == "00000003" { print $1, $3, $5, $6, $7 }' \
          | tr '\n' ';')
    check "a relative jump to an import is measured from the byte after it" "$got" \
        "00000001 R_Z80_8_PCREL ext - 1;00000003 R_Z80_8_PCREL ext + 1;"
    for at in 050000 7A0000; do
        "$ZAP" -c "$OUT/kinds_flat.s" "$OUT/kinds.flat" -o "$at" > /dev/null 2>&1
        "$BIN-ld" -e 0 -Ttext="0x$at" --oformat binary -o "$OUT/kinds.lnk" \
            "$OUT/kinds_x.o" "$OUT/kinds.o" > /dev/null 2>&1
        check "every kind of relocation, linked at $at, is the flat assembly" \
            "$(cmp -s "$OUT/kinds.lnk" "$OUT/kinds.flat" && echo same)" same
    done
else
    echo "FAIL  kinds: $(cat "$OUT/kinds.log" "$OUT/kinds_x.log" 2>/dev/null | tr -d '\r')"
    status=1
fi

# Two objects, one calling into the other, linked at two addresses: the
# result is exactly the flat assembly of the two sources one after the other.
cat > "$OUT/main.s" <<'EOF'
        XREF    lib_add, lib_tbl
        XDEF    main
main:   ld hl, lib_tbl + 3
        call lib_add
        jp main
        dl lib_tbl, lib_add - 1
EOF
cat > "$OUT/lib.s" <<'EOF'
        XDEF    lib_add, lib_tbl
lib_add: add hl, de
        ret
lib_tbl: dl lib_add, lib_tbl, 7
EOF
grep -v 'XREF\|XDEF' "$OUT/main.s" > "$OUT/both.s"
grep -v 'XREF\|XDEF' "$OUT/lib.s" >> "$OUT/both.s"
if obj main && obj lib; then
    for at in 050000 7A0000; do
        "$ZAP" -c "$OUT/both.s" "$OUT/both.flat" -o "$at" > /dev/null 2>&1
        "$BIN-ld" -e 0 -Ttext="0x$at" --oformat binary -o "$OUT/both.lnk" \
            "$OUT/main.o" "$OUT/lib.o" > /dev/null 2>&1
        check "two objects linked at $at are their flat assembly" \
            "$(cmp -s "$OUT/both.lnk" "$OUT/both.flat" && echo same)" same
    done
else
    echo "FAIL  main/lib: $(tr -d '\r' < "$OUT/main.log") $(tr -d '\r' < "$OUT/lib.log")"
    status=1
fi

# Segments that refer to each other, placed by a linker script in the order
# text, rodata, data: the flat assembly of the same lines in that order.
cat > "$OUT/xseg.s" <<'EOF'
start:  ld hl, tbl
        ld de, msg + 1
        SEGMENT DATA
tbl:    dl start, fin, msg
        SEGMENT RODATA
msg:    db "ab"
        SEGMENT CODE
fin:    ret
EOF
cat > "$OUT/xseg_flat.s" <<'EOF'
start:  ld hl, tbl
        ld de, msg + 1
fin:    ret
msg:    db "ab"
tbl:    dl start, fin, msg
EOF
if obj xseg; then
    for at in 050000 7A0000; do
        printf 'SECTIONS { . = 0x%s; .text : { *(.text) } .rodata : { *(.rodata) } .data : { *(.data) } }\n' \
            "$at" > "$OUT/xseg.ld"
        "$ZAP" -c "$OUT/xseg_flat.s" "$OUT/xseg.flat" -o "$at" > /dev/null 2>&1
        "$BIN-ld" -e 0 -T "$OUT/xseg.ld" --oformat binary -o "$OUT/xseg.lnk" \
            "$OUT/xseg.o" > /dev/null 2>&1
        check "segments referring to each other, linked at $at" \
            "$(cmp -s "$OUT/xseg.lnk" "$OUT/xseg.flat" && echo same)" same
    done
else
    echo "FAIL  xseg: $(tr -d '\r' < "$OUT/xseg.log")"
    status=1
fi

# ----------------------------------------------------------------------
# The corpus. Every source zap assembles flat is assembled again as an
# object, and one that is accepted is linked at two addresses and has to be
# exactly its flat assembly at each: every call, jump and address in it is a
# relocation, and a wrong one moves bytes at one address or the other.
#
# A source whose directory holds a single ORG has it removed first, since an
# object has no address of its own; Rokky is one of these. A source refused
# as an object is counted and not failed -- a Z80-mode program, a relocation
# kind not written yet -- but the accepted count has a floor, so this cannot
# quietly stop checking anything. A flat file ends where its last byte is
# written and an object keeps a trailing reservation, so only the flat
# file's length is compared.
# ----------------------------------------------------------------------
corpus_ok=0
corpus_bad=0
corpus_refused=0
corpus_relocs=0
rokky=no
W="$OUT/cw"
for dir in test/corpus/*/ test/regress/*/; do
    [ -d "$dir/tests" ] || continue
    for src in "$dir"/tests/*.s; do
        [ -f "$src" ] || continue
        base=$(basename "$src" .s)
        rm -rf "$W"
        mkdir -p "$W"
        cp -r "$dir"/tests/* "$W/"
        (cd "$W" && timeout 30 "$ZAP" -ez80 "$base.s" f.bin > /dev/null 2>&1) || continue
        [ -f "$W/f.bin" ] || continue

        orgfiles=$(grep -liE '^[[:space:]]*\.?org[[:space:]]' "$W"/* 2>/dev/null || true)
        norg=0
        [ -n "$orgfiles" ] && norg=$(cat $orgfiles | grep -ciE '^[[:space:]]*\.?org[[:space:]]')
        if [ "$norg" = 1 ]; then
            sed -i -E '/^[[:space:]]*\.?[oO][rR][gG][[:space:]]/d' $orgfiles
        fi

        (cd "$W" && timeout 30 "$ZAP" -ez80 -f elf "$base.s" o.o > /dev/null 2>&1) || true
        if [ ! -f "$W/o.o" ]; then
            corpus_refused=$((corpus_refused + 1))
            continue
        fi
        good=1
        for at in 040000 7A0000; do
            rm -f "$W/f.bin" "$W/l.bin"
            (cd "$W" && "$ZAP" -ez80 "$base.s" f.bin -o "$at" > /dev/null 2>&1)
            "$BIN-ld" -e 0 -Ttext="0x$at" --oformat binary -o "$W/l.bin" \
                "$W/o.o" > /dev/null 2>&1
            if [ ! -f "$W/f.bin" ] || [ ! -f "$W/l.bin" ] \
               || ! cmp -s -n "$(stat -c%s "$W/f.bin")" "$W/l.bin" "$W/f.bin"; then
                good=0
            fi
        done
        if [ "$good" = 1 ]; then
            corpus_ok=$((corpus_ok + 1))
            corpus_relocs=$((corpus_relocs + $("$BIN-readelf" -r "$W/o.o" | grep -c R_Z80 || true)))
            [ "$base" = rokky ] && rokky=yes
        else
            corpus_bad=$((corpus_bad + 1))
            echo "FAIL  corpus ${dir#test/}$base: linked bytes differ from flat"
            status=1
        fi
    done
done
echo "      corpus: $corpus_ok linked identically, $corpus_bad differed, $corpus_refused refused as objects, $corpus_relocs relocations"
check "no corpus object links differently from its flat assembly" "$corpus_bad" 0
check "at least 90 corpus sources were linked and compared" \
    "$([ "$corpus_ok" -ge 90 ] && echo yes || echo "only $corpus_ok")" yes
check "Rokky links at two addresses to its flat bytes" "$rokky" yes

# ----------------------------------------------------------------------
# Refusals. Each one is a source that must not produce an object, and the
# message that says why.
# ----------------------------------------------------------------------
refused() {
    local name="$1" src="$2" want="$3"
    printf '%b' "$src" > "$OUT/r.s"
    rm -f "$OUT/r.o"
    local msg
    msg=$("$ZAP" -c -f elf "$OUT/r.s" "$OUT/r.o" 2>&1 | tr -d '\r' || true)
    if [ -f "$OUT/r.o" ]; then
        echo "FAIL  $name: assembled"
        status=1
    elif printf '%s' "$msg" | grep -qF -- "$want"; then
        echo "PASS  $name is refused"
    else
        echo "FAIL  $name: '$(printf '%s' "$msg" | grep -m1 'line')', want '$want'"
        status=1
    fi
}
refused "ORG" '  org 0x40000\n  nop\n' "no address until it is linked"
refused "RELOCATE" '  relocate 0x1000\n  nop\n  endrelocate\n' "no address until it is linked"
refused "ASSUME ADL=0" '  assume adl=0\n' "ADL code only"
refused ".cpu z80" '  .cpu z80\n' "ADL code only"
refused ".cpu z180" '  .cpu z180\n' "ADL code only"
refused "an instruction in the bss" '  segment bss\n  nop\n' "the bss holds no bytes"
refused "DB in the bss" '  .bss\n  db 1\n' "the bss holds no bytes"
refused "a written DS in the bss" '  .bss\n  ds 2\n  blkb 1, 0\n' "the bss holds no bytes"
refused "an unknown segment" '  segment strsect\n' "unknown segment"
refused "an unknown section" '  .section .text.hot\n' "unknown segment"
refused "ALIGN past 32 KB" '  align 0x10000\n' "alignment too large"
refused "EQU of a label" 'lab: nop\nlb2: equ lab\n' "not known until it is linked"
refused "EQU of \$" 'lb2: equ $\n' "not known until it is linked"
refused "DS of a label" 'lab: nop\n  ds lab\n' "not known until it is linked"
refused "ALIGN of a label" 'lab: nop\n  align lab\n' "not known until it is linked"
refused "IF on a label" 'lab: nop\n  if lab\n  endif\n' "not known until it is linked"
refused "an address in DW32" 'lab: nop\n  dw32 lab\n' "cannot be used this way"
refused "an import subtracted" '  xref ext\nlab: nop\n  dl lab - ext\n' "cannot be used this way"
refused "two imports added" '  xref e1, e2\n  dl e1 + e2\n' "cannot be used this way"
refused "XREF of a label defined here" '  xref lab\nlab: nop\n' "imported with XREF is defined here"
refused "XREF after the definition" 'lab: nop\n  xref lab\n' "imported with XREF is defined here"
refused "EQU of an import" '  xref lab\nlab: equ 5\n' "imported with XREF is defined here"
refused "XDEF of a label never defined" '  xdef nowhere\n  nop\n' "exported with XDEF is never defined"
refused "XDEF of a local" 'lab: nop\n  xdef @loc\n@loc: nop\n' "local label cannot be exported"
refused "XREF of a local" '  xref @loc\n' "local label cannot be exported"
refused "a label both exported and imported" '  xdef lab\n  xref lab\nlab: nop\n' "both exported and imported"
refused "XDEF with no name" '  xdef\n' "expected a label name"
refused "XDEF with a trailing comma" 'lab: nop\n  xdef lab,\n' "expected a label name"
refused "a relative jump to a number" '  jr n\nn: equ 5\n' "cannot be used this way"
refused "a byte of an address in a wider field" 'lab: nop\n  ld hl, lab >> 8\n' "cannot be used this way"
refused "a byte of an address with more added" 'lab: nop\n  ld a, (lab >> 8) + 1\n' "cannot be used this way"
refused "a shift a relocation cannot take" 'lab: nop\n  ld a, lab >> 4\n' "cannot be used this way"
refused "a mask a relocation cannot take" 'lab: nop\n  ld a, lab & 0xF0\n' "cannot be used this way"
refused "an address multiplied" 'lab: nop\n  ld hl, lab * 2\n' "cannot be used this way"
refused "an address negated" 'lab: nop\n  ld hl, -lab\n' "cannot be used this way"
refused "a label ahead multiplied" '  ld hl, lab * 2\nlab: nop\n' "cannot be used this way"
refused "a relative jump to a byte of an address" 'lab: nop\n  jr lab >> 8\n' "cannot be used this way"
refused "EQU of a distance across segments" 'lab: nop\n  .data\nlb2: db 0\nd: equ lb2 - lab\n' "not known until it is linked"
refused "a bit number from a label" 'lab: nop\n  bit lab, a\n' "cannot be used this way"
refused "an index offset from a label" 'lab: nop\n  ld a, (ix+lab)\n' "cannot be used this way"
refused "the difference of two segments" 'lab: nop\n  .data\nlb2: db 0\n  dl lb2 - lab\n' "cannot be used this way"
refused "the sum of two addresses" 'lab: nop\nlb2: nop\n  dl lab + lb2\n' "cannot be used this way"
refused "a label nothing defines" '  dl nowhere\n' "unknown label"

# Options that mean nothing in an object, or are not written for one yet.
printf '  nop\n' > "$OUT/opt.s"
for bad in "-o 50000" "-a 0" "-l" "-d" "-s"; do
    # shellcheck disable=SC2086
    m=$("$ZAP" -c -f elf $bad "$OUT/opt.s" "$OUT/opt.o" 2>&1 | tr -d '\r' || true)
    check "$bad with -f is refused" "$(printf '%s' "$m" | grep -c 'cannot be used with -f')" 1
done
for f in "acc" "coff" ""; do
    # shellcheck disable=SC2086
    m=$("$ZAP" -c "$OUT/opt.s" "$OUT/opt.o" -f $f 2>&1 | tr -d '\r' || true)
    check "-f '$f' is refused" "$(printf '%s' "$m" | grep -c '^Option -f')" 1
done
m=$("$ZAP" -c "$OUT/opt.s" "$OUT/opt.o" -f coff 2>&1 | tr -d '\r' || true)
check "-f with an unknown format names it" \
    "$(printf '%s' "$m" | grep -c 'unknown format "coff"')" 1
m=$("$ZAP" -c "$OUT/opt.s" "$OUT/opt.o" -f 2>&1 | tr -d '\r' || true)
check "-f with no format says one is needed" \
    "$(printf '%s' "$m" | grep -c 'needs a format')" 1
check "-felf attached is taken" \
    "$("$ZAP" -c -felf "$OUT/opt.s" "$OUT/opt.o" 2>&1 | tr -d '\r' | grep -c '^Wrote ')" 1
check "-x counts the object file's bytes" \
    "$("$ZAP" -c -x -f elf "$OUT/opt.s" "$OUT/opt.o" 2>&1 | tr -d '\r' \
       | awk '$1 == "Output" && $2 == ":" { print $3 }')" "$(wc -c < "$OUT/opt.o" | tr -d ' ')"

# Without -f the new directives are ordinary words, as they are to ez80asm:
# refused as instructions, or free to be a macro's name.
printf '  segment code\n' > "$OUT/flat1.s"
check "SEGMENT without -f is not a directive" \
    "$("$ZAP" -c "$OUT/flat1.s" "$OUT/flat1.bin" 2>&1 | tr -d '\r' | grep -c 'unknown instruction')" 1
printf 'macro segment x\n  db x\nendmacro\n  segment 7\n' > "$OUT/flat2.s"
"$ZAP" -c "$OUT/flat2.s" "$OUT/flat2.bin" > /dev/null 2>&1 || true
check "a macro named segment still works without -f" "$(xxd -p "$OUT/flat2.bin" 2>/dev/null)" "07"

exit $status
