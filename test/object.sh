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
refused "an address in an instruction" 'lab: nop\n  ld hl, lab\n' "cannot write yet"
refused "an address ahead in an instruction" '  call lab\nlab: nop\n' "cannot write yet"
refused "an address in DL" 'lab: nop\n  dl lab\n' "cannot write yet"
refused "an address in DB" 'lab: nop\n  db lab\n' "cannot write yet"
refused "a byte of an address" 'lab: nop\n  ld a, lab >> 8\n' "cannot write yet"
refused "\$ as an address" '  ld hl, $\n' "cannot write yet"
refused "@b as an address" '@@: ld hl, @b\n' "cannot write yet"
refused "@b as an address in an expression" '@@: dl @b + 1\n' "cannot write yet"
refused "a jump into another segment" '  jr lab\n  .data\nlab: db 0\n' "cannot write yet"
refused "a relative jump to a number" '  jr n\nn: equ 5\n' "cannot write yet"
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
