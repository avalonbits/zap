#!/bin/bash
# Host test runner.
#
# zap is a single translation unit with its own main, and everything in it is
# static, so there is no library for a test to link against: a test that needs
# the internals includes zap.c and renames main out of the way. That is why
# zap.c is not in SRCS below -- each test that wants it brings its own copy,
# and the ones that only exercise a shared file (test_number.c, test_timing.c)
# link it from there.
#
# Three kinds of check, in order: the unit tests, the command line, and every
# source in test/cases assembled against the vendored ez80asm and compared byte
# for byte. The last of those is the compatibility claim and the one that
# catches what nobody thought to write a unit test for.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# -fsigned-char because char is signed on the eZ80, and host_types.h because
# uint24_t is a builtin there with no header.
CFLAGS=(-std=gnu11 -Wall -Wextra -fsigned-char -g -fsanitize=address,undefined
        -include "$ROOT/test/stubs/host_types.h" -Isrc
        -I"$ROOT/test/stubs")

# The shared sources zap links, minus zap.c itself.
SRCS=(src/buf_reader.c src/value.c src/conv.c src/isa_table.c
      "$ROOT/test/stubs/agon_stubs.c")

status=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    echo "=== $name ==="
    cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" "${SRCS[@]}"
    "$OUT/$name" || status=$?
done

# The CLI, which the unit tests cannot reach, and which has to keep printing
# what ez80asm prints: the timing line exists to be compared against the
# reference's and a format that drifts stops being comparable.
echo "=== test_cli ==="
cli_check() {
    if [ "$2" = "$3" ]; then
        echo "PASS  $1"
    else
        echo "FAIL  $1: got '$2', want '$3'"
        status=1
    fi
}

cc "${CFLAGS[@]}" -o "$OUT/zap" src/zap.c "${SRCS[@]}"
printf '  nop\n  ret\n' > "$OUT/ok.s"
printf '  ld a,\n' > "$OUT/bad.s"

out=$("$OUT/zap" "$OUT/ok.s" "$OUT/ok.bin" 2>&1 | tr -d '\r')
cli_check "assembling line"  "$(printf '%s' "$out" | grep -c '^Assembling ')" 1
cli_check "wrote line"       "$(printf '%s' "$out" | grep -c '^Wrote .*, 2 bytes')" 1
cli_check "timing line matches the reference's format" \
    "$(printf '%s' "$out" | grep -cE '^Done in [0-9]+\.[0-9][0-9] seconds$')" 1

bad=$("$OUT/zap" "$OUT/bad.s" "$OUT/bad.bin" 2>&1 | tr -d '\r' || true)
cli_check "no timing on failure" "$(printf '%s' "$bad" | grep -c '^Done in ')" 0

# An error inside an included file has to name that file, not the one that
# included it -- and the name has to still be there to print. It lives in the
# frame of the include that opened it, and by the time the report happens every
# one of those frames has unwound; before this was fixed it printed as
# `inc_ .in`.
printf '  ld a,\n' > "$OUT/broken.inc"
printf '  INCLUDE "%s"\n' "$OUT/broken.inc" > "$OUT/inctop.s"
incbad=$("$OUT/zap" "$OUT/inctop.s" "$OUT/inc.bin" 2>&1 | tr -d '\r' || true)
cli_check "an error names the included file" \
    "$(printf '%s' "$incbad" | grep -c "^File \"$OUT/broken.inc\" line 1 - ")" 1

# The report itself, which is the thing a user actually meets.
#
# Every part of it is captured when the failure happens and none of it is kept
# in advance, so a source that assembles pays for none of this. That is the
# property worth protecting; these check that the parts arrive.
printf '  frobnicate a\n' > "$OUT/r1.s"
r1=$("$OUT/zap" "$OUT/r1.s" "$OUT/r1.bin" 2>&1 | tr -d '\r' || true)
cli_check "the message names the token it is about" \
    "$(printf '%s' "$r1" | grep -c "unknown instruction 'frobnicate'")" 1
cli_check "the failing line is echoed" \
    "$(printf '%s' "$r1" | grep -c '^  frobnicate a$')" 1

# A label nothing defines is found when the fixups are patched, long after the
# line has gone -- so the line is fetched back out of the file, which costs one
# open and costs it only when the assembly has already failed.
printf '  nop\n  nop\n  jp nowhere\n' > "$OUT/r2.s"
r2=$("$OUT/zap" "$OUT/r2.s" "$OUT/r2.bin" 2>&1 | tr -d '\r' || true)
cli_check "a deferred failure still names its line" \
    "$(printf '%s' "$r2" | grep -c "line 3 - unknown label 'nowhere'")" 1
cli_check "a deferred failure still shows its line" \
    "$(printf '%s' "$r2" | grep -c '^  jp nowhere$')" 1

# A failure inside a macro body, which is the report that was unusable before:
# it named the macro as though it were a file, counted lines from the top of
# the body, and said nothing at all about where the macro had been invoked.
printf '  MACRO m v\n  ld a, v v\n  ENDMACRO\n  nop\n  m 5\n' > "$OUT/r3.s"
r3=$("$OUT/zap" "$OUT/r3.s" "$OUT/r3.bin" 2>&1 | tr -d '\r' || true)
cli_check "a macro failure names the macro and the line of the file" \
    "$(printf '%s' "$r3" | grep -c '^Macro \[m\] in .* line 2 - ')" 1
cli_check "a macro failure shows the body line" \
    "$(printf '%s' "$r3" | grep -c '^ld a, 5 5$')" 1
cli_check "a macro failure names where it was invoked" \
    "$(printf '%s' "$r3" | grep -c '^Invoked from .* line 5 as$')" 1
cli_check "a macro failure shows the invocation" \
    "$(printf '%s' "$r3" | grep -c '^  m 5$')" 1

# Colour is asked for, never assumed: the corpus runner and every grep above
# read what this prints.
cli_check "no colour unless it is asked for" \
    "$(printf '%s' "$r1" | grep -c "$(printf '\033')")" 0
r4=$("$OUT/zap" -color "$OUT/r1.s" "$OUT/r1.bin" 2>&1 | tr -d '\r' || true)
cli_check "-color colours the report" \
    "$(printf '%s' "$r4" | grep -c "$(printf '\033')\[31m")" 1
cli_check "-colour is the same option" \
    "$("$OUT/zap" -colour "$OUT/r1.s" "$OUT/r1.bin" 2>&1 | tr -d '\r' \
       | grep -c "$(printf '\033')\[31m")" 1
# The message and the token it quotes are coloured differently, as they are in
# the reference, so the escape falls between them -- which is why this looks
# for the message alone rather than for the whole line.
cli_check "the colour flag does not change what is said" \
    "$(printf '%s' "$r4" | grep -c 'unknown instruction')" 1
cli_check "the colour flag does not change the token" \
    "$(printf '%s' "$r4" | grep -c "'frobnicate'")" 1

# mnemonic_of compares without checking the length, which is only safe while
# every name in a bucket has the same length. build_tables says so if that ever
# stops being true; nothing else would notice until an instruction assembled as
# a different one.
#
# Note what this does *not* say. Buckets hold several mnemonics on purpose --
# adc, add and and are all in the one keyed by `a` and length 3, and the
# longest chain is twelve. What has to hold is that they agree on length, which
# is what lets the compare skip it. The check was called "no two mnemonics
# share a bucket" for some time, which is a different and false claim, and
# reading it as true led to an afternoon of reasoning about a chain walk that
# does not exist.
cli_check "mnemonics in a bucket agree on length" \
    "$(printf '%s' "$out" | grep -c 'share a bucket')" 0

# The mode groups are held in a fixed table, sized to the table that exists. An
# overflow would silently drop the groups that did not fit and every mnemonic
# after it would stop matching anything, so build_tables says so instead.
cli_check "the mode groups fit their table" \
    "$(printf '%s' "$out" | grep -c 'mode groups')" 0

# same_ci case-folds the source and not the table, so a capital in a mnemonic
# would make that one instruction unmatchable and nothing else would say why.
cli_check "every mnemonic in the table is lower case" \
    "$(printf '%s' "$out" | grep -c 'not lower case')" 0
cli_check "failure is reported"  "$(printf '%s' "$bad" | grep -c 'line 1 - ')" 1

# Which failure, not just that there was one. Any trailing text errors
# eventually -- the operand parser rejects a bare token, and a token that got
# past it becomes the next line's mnemonic and is rejected there -- so the
# encoding tests, which see only ERR, cannot tell the line loop's own check
# from those. Deleting it failed no test at all until this one.
printf '  ld a, b c\n' > "$OUT/trail.s"
trail=$("$OUT/zap" "$OUT/trail.s" "$OUT/trail.bin" 2>&1 | tr -d '\r' || true)
cli_check "trailing text is reported by the line loop" \
    "$(printf '%s' "$trail" | grep -c 'line 1 - unexpected text after the instruction')" 1

# The token a name run hands to the literal path.
#
# A name that is not a register is re-read as a literal, and that second scan
# is skipped when it would read the same characters -- which it does unless the
# character that ended the name run is one of $, # or %, the three C_NUM has
# that C_NAME does not. Skipping it unconditionally still errors on `ab$cd`,
# just as the wrong thing: the token becomes `ab` and `$cd` is trailing text.
# Only the message tells the two apart, so only the message can test it.
printf '  ld a, ab$cd\n' > "$OUT/dollar.s"
dollar=$("$OUT/zap" "$OUT/dollar.s" "$OUT/dollar.bin" 2>&1 | tr -d '\r' || true)
cli_check "a name run stopping at \$ is scanned as one token" \
    "$(printf '%s' "$dollar" | grep -c 'line 1 - unknown label')" 1

# An undefined local is reported against the line that used it.
#
# It is discovered somewhere else entirely -- when the next global label ends
# the scope, which here is three lines later -- so the line number has to come
# off the pending reference rather than from wherever the failure surfaced.
# Nothing in the encoding tests can see a line number.
printf 'one:\n  nop\n  jp @gone\n  nop\n  nop\ntwo:\n  nop\n' > "$OUT/loc.s"
loc=$("$OUT/zap" "$OUT/loc.s" "$OUT/loc.bin" 2>&1 | tr -d '\r' || true)
cli_check "an undefined local names the line that used it" \
    "$(printf '%s' "$loc" | grep -c 'line 3 - unknown label')" 1

# The thirteen refusals the reference makes and this one did not. None of them
# changes the bytes of a valid program; each is a diagnostic that was missing.
# They produce no output, so only a message can tell them from any other
# refusal.
lab64=$(printf 'a%.0s' $(seq 1 64))
lab65=$(printf 'a%.0s' $(seq 1 65))
printf '%s: nop\n' "$lab64" > "$OUT/lab1.s"
"$OUT/zap" "$OUT/lab1.s" "$OUT/lab1.bin" > /dev/null 2>&1 || true
cli_check "a label of 64 characters is allowed" \
    "$(od -An -tx1 "$OUT/lab1.bin" 2>/dev/null | tr -s ' ')" " 00"
printf '%s: nop\n' "$lab65" > "$OUT/lab2.s"
lab2=$("$OUT/zap" "$OUT/lab2.s" "$OUT/lab2.bin" 2>&1 | tr -d '\r' || true)
cli_check "a label of 65 characters is refused" \
    "$(printf '%s' "$lab2" | grep -c 'line 1 - label too long')" 1

# The `@` counts towards the limit, so a local has one character less of name.
printf 'g:\n@%s: nop\n' "$lab64" > "$OUT/lab3.s"
lab3=$("$OUT/zap" "$OUT/lab3.s" "$OUT/lab3.bin" 2>&1 | tr -d '\r' || true)
cli_check "the at sign counts towards the limit" \
    "$(printf '%s' "$lab3" | grep -c 'line 2 - label too long')" 1

# One signed byte is what the instruction has room for, so anything else would
# be emitted truncated and silently wrong.
printf '  ld a,(ix+127)\n  ld a,(ix-128)\n' > "$OUT/dsp1.s"
"$OUT/zap" "$OUT/dsp1.s" "$OUT/dsp1.bin" > /dev/null 2>&1 || true
cli_check "the ends of the displacement range are allowed" \
    "$(od -An -tx1 "$OUT/dsp1.bin" 2>/dev/null | tr -s ' ')" " dd 7e 7f dd 7e 80"
for d in '+128' '-129'; do
    printf '  ld a,(ix%s)\n' "$d" > "$OUT/dsp2.s"
    dsp2=$("$OUT/zap" "$OUT/dsp2.s" "$OUT/dsp2.bin" 2>&1 | tr -d '\r' || true)
    cli_check "a displacement of $d is refused" \
        "$(printf '%s' "$dsp2" | grep -c 'line 1 - index offset out of range')" 1
done

# A macro parameter may not be anything the body could not tell from what it
# stands for: a number in any radix, or a mnemonic or directive. Registers are
# allowed, and `macro m hl` assembles in both.
for bad in 1 1h 0x1 0b1 1b and ld db equ macro align; do
    printf '  macro m %s\n  db 5\n  endmacro\n' "$bad" > "$OUT/arg.s"
    arg=$("$OUT/zap" "$OUT/arg.s" "$OUT/arg.bin" 2>&1 | tr -d '\r' || true)
    cli_check "a macro parameter called $bad is refused" \
        "$(printf '%s' "$arg" | grep -c 'line 1 - a macro parameter may not be')" 1
done
for ok in hl nz af x1 _x; do
    printf '  macro m %s\n  db %s\n  endmacro\n  m 5\n' "$ok" "$ok" > "$OUT/argok.s"
    "$OUT/zap" "$OUT/argok.s" "$OUT/argok.bin" > /dev/null 2>&1 || true
    cli_check "a macro parameter called $ok is allowed" \
        "$(od -An -tx1 "$OUT/argok.bin" 2>/dev/null | tr -s ' ')" " 05"
done

# And the same macro name twice, which is case-blind as the lookup is.
printf '  macro m\n  ld a,b\n  endmacro\n  macro M\n  ld b,c\n  endmacro\n' > "$OUT/dup.s"
dup=$("$OUT/zap" "$OUT/dup.s" "$OUT/dup.bin" 2>&1 | tr -d '\r' || true)
cli_check "a macro defined twice is refused" \
    "$(printf '%s' "$dup" | grep -c 'line 4 - that macro is already defined')" 1

# An anonymous label in a body, refused at the invocation as a global one is.
printf '  macro m\n  ld a,b\n@@: db 5\n  endmacro\n  m\n' > "$OUT/anon.s"
anon=$("$OUT/zap" "$OUT/anon.s" "$OUT/anon.bin" 2>&1 | tr -d '\r' || true)
cli_check "an anonymous label in a macro is refused" \
    "$(printf '%s' "$anon" | grep -c 'no anonymous labels allowed in a macro')" 1

# A FILLBYTE that would change the fill of a reservation already written.
#
# The reference fills a reservation when it writes the file out, so the last
# FILLBYTE wins for every one of them, backwards as well. One pass writes the
# bytes where it meets them; reproducing that means remembering every reserved
# range to go back over, for a case that appears nowhere in the reference's own
# corpus, where every FILLBYTE precedes the reservations it is for.
printf '  ds 2\n  fillbyte 0xAA\n  nop\n' > "$OUT/fb1.s"
fb1=$("$OUT/zap" "$OUT/fb1.s" "$OUT/fb1.bin" 2>&1 | tr -d '\r' || true)
cli_check "a FILLBYTE that reaches backwards is refused" \
    "$(printf '%s' "$fb1" | grep -c 'line 2 - FILLBYTE must come before the space it fills')" 1

# The same value twice is not a change, so it is allowed.
printf '  fillbyte 0xAA\n  ds 2\n  fillbyte 0xAA\n  nop\n' > "$OUT/fb2.s"
"$OUT/zap" "$OUT/fb2.s" "$OUT/fb2.bin" > /dev/null 2>&1 || true
cli_check "the same FILLBYTE twice is not a change" \
    "$(od -An -tx1 "$OUT/fb2.bin" 2>/dev/null | tr -s ' ')" " aa aa 00"

# RELOCATE, whose three refusals the reference also makes.
#
# `$1000000` is the corpus's own spelling of the address one past the eZ80's
# range. The `0x` spelling of the same number is *not* caught, and that is the
# 24-bit ceiling showing through rather than anything about RELOCATE: the `0x`
# fast path accumulates in the machine's word and truncates, where the `$`
# prefix goes through the general parser and the wide value survives to be
# checked. Recorded in .internal/completeness.md with the rest of the ceiling.
printf '  .relocate $1000000\n  .endrelocate\n' > "$OUT/rl1.s"
rl1=$("$OUT/zap" "$OUT/rl1.s" "$OUT/rl1.bin" 2>&1 | tr -d '\r' || true)
cli_check "a relocate address past 24 bits is refused" \
    "$(printf '%s' "$rl1" | grep -c 'line 1 - address outside the 24-bit range')" 1

printf '  .relocate -1\n  .endrelocate\n' > "$OUT/rl2.s"
rl2=$("$OUT/zap" "$OUT/rl2.s" "$OUT/rl2.bin" 2>&1 | tr -d '\r' || true)
cli_check "a negative relocate address is refused" \
    "$(printf '%s' "$rl2" | grep -c 'line 1 - address outside the 24-bit range')" 1

printf '  .endrelocate\n' > "$OUT/rl3.s"
rl3=$("$OUT/zap" "$OUT/rl3.s" "$OUT/rl3.bin" 2>&1 | tr -d '\r' || true)
cli_check "ENDRELOCATE with none open is refused" \
    "$(printf '%s' "$rl3" | grep -c 'line 1 - no RELOCATE is open')" 1

printf '  .relocate 0x50000\n  .relocate 0x60000\n' > "$OUT/rl4.s"
rl4=$("$OUT/zap" "$OUT/rl4.s" "$OUT/rl4.bin" 2>&1 | tr -d '\r' || true)
cli_check "a nested RELOCATE is refused" \
    "$(printf '%s' "$rl4" | grep -c 'line 2 - RELOCATE does not nest')" 1

# .CPU selects an instruction set, and the bytes it produces are in
# test/cases/cpu.s against the reference. These are the refusals, which have
# no bytes and so can only be told apart by their message.
#
# The rows the filter takes away. `ld ixh, b` is an eZ80 form, assembles at the
# top of cpu.s, and is 260 of the reference's own corpus sources under Z180.
printf '  .cpu Z180\n  ld ixh, b\n' > "$OUT/cpu1.s"
cpu1=$("$OUT/zap" "$OUT/cpu1.s" "$OUT/cpu1.bin" 2>&1 | tr -d '\r' || true)
cli_check "an eZ80 form is gone under .cpu Z180" \
    "$(printf '%s' "$cpu1" | grep -c 'line 2 - no such instruction form')" 1

# Neither the Z80 nor the Z180 has ADL, so there is no mode to select and no
# suffix to select it with. The reference refuses ADL=0 as well as ADL=1,
# though it names the mode they are already in.
for mode in 0 1; do
    printf '  .cpu Z80\n  .assume ADL=%s\n' "$mode" > "$OUT/cpu2.s"
    cpu2=$("$OUT/zap" "$OUT/cpu2.s" "$OUT/cpu2.bin" 2>&1 | tr -d '\r' || true)
    cli_check "ADL=$mode is refused on the Z80" \
        "$(printf '%s' "$cpu2" | grep -c 'line 2 - no ADL mode on this CPU')" 1
done
printf '  .cpu Z80\n  ld.lil a,(0)\n' > "$OUT/cpu3.s"
cpu3=$("$OUT/zap" "$OUT/cpu3.s" "$OUT/cpu3.bin" 2>&1 | tr -d '\r' || true)
cli_check "a mode suffix is refused on the Z80" \
    "$(printf '%s' "$cpu3" | grep -c 'line 2 - no mode suffix on this CPU')" 1

# The Z280 has a bit in the table and no rows tagged with it, so it is not
# offered rather than accepted and then quietly empty.
printf '  .cpu Z280\n  nop\n' > "$OUT/cpu4.s"
cpu4=$("$OUT/zap" "$OUT/cpu4.s" "$OUT/cpu4.bin" 2>&1 | tr -d '\r' || true)
cli_check "an unsupported CPU is refused" \
    "$(printf '%s' "$cpu4" | grep -c 'line 1 - unsupported CPU type')" 1

# The three-operand form is RES and SET and nothing else, and the bit is spent
# on the pseudo mnemonic -- so nothing downstream would notice an eighth bit,
# and nothing but the lookup stops `ld a, b, c`.
printf '  .cpu Z80\n  res 8,(ix+0),b\n' > "$OUT/cpu5.s"
cpu5=$("$OUT/zap" "$OUT/cpu5.s" "$OUT/cpu5.bin" 2>&1 | tr -d '\r' || true)
cli_check "a bit outside 0..7 has no third-operand form" \
    "$(printf '%s' "$cpu5" | grep -c 'line 2 - no such instruction form')" 1
printf '  ld a, b, c\n' > "$OUT/cpu6.s"
cpu6=$("$OUT/zap" "$OUT/cpu6.s" "$OUT/cpu6.bin" 2>&1 | tr -d '\r' || true)
cli_check "a third operand on an instruction that has none is refused" \
    "$(printf '%s' "$cpu6" | grep -c 'line 1 - no such instruction form')" 1
printf '  .cpu Z80\n  bit 0,(ix+0),b\n' > "$OUT/cpu7.s"
cpu7=$("$OUT/zap" "$OUT/cpu7.s" "$OUT/cpu7.bin" 2>&1 | tr -d '\r' || true)
cli_check "BIT has no third-operand form, because it writes no result" \
    "$(printf '%s' "$cpu7" | grep -c 'line 2 - no such instruction form')" 1

# The mask is a file-scope static and the unit tests assemble many sources in
# one process, so it has to be reset with the rest of the state. Two files,
# the second of which would fail to encode if the first one's .cpu leaked.
printf '  .cpu Z80\n  sll b\n' > "$OUT/cpu8.s"
"$OUT/zap" "$OUT/cpu8.s" "$OUT/cpu8.bin" > /dev/null 2>&1 || true
printf '  ld.lil hl, 0x123456\n' > "$OUT/cpu9.s"
cpu9=$("$OUT/zap" "$OUT/cpu9.s" "$OUT/cpu9.bin" 2>&1 | tr -d '\r' || true)
cli_check "an eZ80 file after a Z80 one still assembles" \
    "$(xxd -p "$OUT/cpu9.bin" 2>/dev/null | tr -d '\n')" "5b21563412"

# A negative count, which the reference reads as unsigned: `blkb -1` there is
# sixteen megabytes of fill and a successful assembly. Refused here for the
# reason DS's count is, and only the message tells the two refusals apart.
printf '  blkb -1\n' > "$OUT/blk1.s"
blk1=$("$OUT/zap" "$OUT/blk1.s" "$OUT/blk1.bin" 2>&1 | tr -d '\r' || true)
cli_check "a negative block count is refused" \
    "$(printf '%s' "$blk1" | grep -c 'line 1 - blk needs a positive number')" 1

# A fill that names a label still ahead. It is one value repeated n times, so
# there is nothing a per-byte fixup could usefully do: the run is written now
# and filled in when the value is known, which is one record however long the
# run is.
printf '  blkb 2, ahead\nahead:\n  nop\n' > "$OUT/blk2.s"
"$OUT/zap" "$OUT/blk2.s" "$OUT/blk2.bin" > /dev/null 2>&1 || true
cli_check "a fill still ahead is filled in afterwards" \
    "$(od -An -tx1 "$OUT/blk2.bin" 2>/dev/null | tr -s ' ')" " 02 02 00"

# The count is a different matter and is still refused: how many bytes there
# are decides where everything after them lands.
printf '  blkb ahead, 1\nahead: equ 2\n' > "$OUT/blk2b.s"
blk2b=$("$OUT/zap" "$OUT/blk2b.s" "$OUT/blk2b.bin" 2>&1 | tr -d '\r' || true)
cli_check "a count still ahead is refused" \
    "$(printf '%s' "$blk2b" | grep -c 'line 1 - a label here must be defined already')" 1

# And a fill nothing ever defines is found when the run is filled in.
printf '  blkb 2, nosuch\n' > "$OUT/blk2c.s"
blk2c=$("$OUT/zap" "$OUT/blk2c.s" "$OUT/blk2c.bin" 2>&1 | tr -d '\r' || true)
cli_check "a fill nothing defines is reported" \
    "$(printf '%s' "$blk2c" | grep -c 'unknown label')" 1

# A constant added to a label that is still ahead has to fit the machine's
# word, and this is the one quantity on the expression path that does not
# widen with the evaluator.
#
# The fixup record is sixteen bytes and every one of them is spoken for --
# widening the addend would cost a multiply on every index into the list --
# so it is checked instead of truncated. The bound is written out as a
# constant rather than derived from `int`, which is three bytes on the Agon
# and four here: derived, this would refuse on one machine and accept on the
# other, which is the failure the widening exists to remove. That is why this
# check can bite on the host at all.
printf '  dw32 later + 0x55555555\nlater: EQU 1\n' > "$OUT/wide1.s"
wide1=$("$OUT/zap" "$OUT/wide1.s" "$OUT/wide1.bin" 2>&1 | tr -d '\r' || true)
cli_check "a constant too large to add to a label is refused" \
    "$(printf '%s' "$wide1" | grep -c 'line 1 - that constant is too large')" 1

# The same bound on the other path that folds a constant into an addend: a
# global minus a local, settled when the scope ends rather than where it was
# written.
printf 'g:\n  dw32 ahead - @loc\n@loc: EQU 0x7FFFFFF\nh:\n  nop\nahead: EQU 1\n' \
    > "$OUT/wide2.s"
wide2=$("$OUT/zap" "$OUT/wide2.s" "$OUT/wide2.bin" 2>&1 | tr -d '\r' || true)
cli_check "a folded local half too large to add is refused" \
    "$(printf '%s' "$wide2" | grep -c 'that constant is too large')" 1

# The third thing the machine's word was costing, and the one nobody was
# looking for.
#
# `relocate 0x1000000` is "Address outside 24-bit range" in the reference and
# was accepted here: the range test read `value > 0xFFFFFF`, which a 24-bit
# int can never satisfy, so half of it was dead code on the Agon and live on
# the host. The `$1000000` spelling of the same number went through the
# general parser and *was* caught, which is how it was noticed at all.
#
# Two spellings, because they take different paths to the same value and only
# one of them was ever wrong.
for spell in 0x1000000 '$1000000'; do
    printf '  relocate %s\n  nop\n  endrelocate\n' "$spell" > "$OUT/rel24.s"
    rel24=$("$OUT/zap" "$OUT/rel24.s" "$OUT/rel24.bin" 2>&1 | tr -d '\r' || true)
    cli_check "relocate $spell is outside the 24-bit range" \
        "$(printf '%s' "$rel24" | grep -c 'address outside the 24-bit range')" 1
done

# DW32 and BLKL do not exist below the width they need, so the directives and
# the evaluator ship together. The bytes are in test/cases/data.s, compared
# against the reference; this says the names resolve at all, which a build
# with the directives renumbered wrongly would not.
printf '  dw32 1\n  blkl 1, 2\n' > "$OUT/blk3.s"
"$OUT/zap" "$OUT/blk3.s" "$OUT/blk3.bin" > /dev/null 2>&1 || true
cli_check "dw32 and blkl are four bytes each" \
    "$(xxd -p "$OUT/blk3.bin" 2>/dev/null | tr -d '\n')" "0100000002000000"

# A mode suffix on an instruction whose row does not take one.
#
# The suffix means something only where the instruction touches memory, the
# stack or an address, so `ld.lil hl, nn` assembles and `ld.lil a, b` does not
# -- the reference calls that "Suffix not matching mnemonic / ADL mode". Both
# are the same row table and the same operands; only the message tells them
# apart, and the encoding tests would read every refusal here as ERR.
printf '  ld.lil a, b\n' > "$OUT/sfx1.s"
sfx1=$("$OUT/zap" "$OUT/sfx1.s" "$OUT/sfx1.bin" 2>&1 | tr -d '\r' || true)
cli_check "a suffix on a register-only form is refused" \
    "$(printf '%s' "$sfx1" | grep -c 'line 1 - this instruction takes no mode suffix')" 1

# Per row and not per mnemonic: `retn.lil` assembles, `retn.sis` does not.
printf '  retn.sis\n' > "$OUT/sfx2.s"
sfx2=$("$OUT/zap" "$OUT/sfx2.s" "$OUT/sfx2.bin" 2>&1 | tr -d '\r' || true)
cli_check "a row may take some suffixes and not others" \
    "$(printf '%s' "$sfx2" | grep -c 'line 1 - this instruction takes no mode suffix')" 1

# A dot the suffix reader does not understand is left alone rather than
# refused, which is what sends `.db` to the directives and lets a macro be
# called `read.next`. It arrives as an unknown instruction, not a bad suffix.
printf '  ld.xyz hl, 0\n' > "$OUT/sfx3.s"
sfx3=$("$OUT/zap" "$OUT/sfx3.s" "$OUT/sfx3.bin" 2>&1 | tr -d '\r' || true)
cli_check "an unreadable suffix is not read as one" \
    "$(printf '%s' "$sfx3" | grep -c 'line 1 - unknown instruction')" 1

# And the dot that starts a directive is not a suffix: it is at the front.
printf '  .db 1, 2\n' > "$OUT/sfx4.s"
"$OUT/zap" "$OUT/sfx4.s" "$OUT/sfx4.bin" > /dev/null 2>&1 || true
cli_check "a leading dot still reaches the directives" \
    "$(od -An -tx1 "$OUT/sfx4.bin" 2>/dev/null | tr -s ' ')" " 01 02"

# A string whose last character is a backslash, which is not terminated.
#
# The scan steps two past an escape and no longer asks whether the second one
# is inside the buffer: the reader keeps a newline one byte past the last valid
# character, so the step lands on or past it and the `q < e` test ends the
# scan. Without the message this looks the same as any other refusal, and the
# encoding tests would read both as ERR.
printf '  DB "abc\\' > "$OUT/esc.s"
esc=$("$OUT/zap" "$OUT/esc.s" "$OUT/esc.bin" 2>&1 | tr -d '\r' || true)
cli_check "a string ending in a backslash is not terminated" \
    "$(printf '%s' "$esc" | grep -c 'line 1 - string not terminated')" 1

# A global label in a macro body, which the reference refuses -- "No global
# labels allowed in macro definition" -- and refuses at the invocation rather
# than at the definition, so a body that is never used is never complained
# about. Both halves need a message to be seen: the encoding tests would read
# the refusal and the acceptance as ERR and 00, which is also what a macro that
# was never expanded at all would give.
printf 'g:\n  MACRO m\nglob:\n  nop\n  ENDMACRO\n  m\n' > "$OUT/gmac.s"
gmac=$("$OUT/zap" "$OUT/gmac.s" "$OUT/gmac.bin" 2>&1 | tr -d '\r' || true)
cli_check "a global label in an expanded macro is refused" \
    "$(printf '%s' "$gmac" | grep -c 'no global labels allowed in a macro')" 1
printf 'g:\n  MACRO m\nglob:\n  nop\n  ENDMACRO\n  nop\n' > "$OUT/gmac2.s"
gmac2=$("$OUT/zap" "$OUT/gmac2.s" "$OUT/gmac2.bin" 2>&1 | tr -d '\r' || true)
cli_check "the same body, never invoked, is not" \
    "$(printf '%s' "$gmac2" | grep -c 'no global labels')" 0

# The reference itself, on everything in test/cases. Unit tests pin the cases a
# refactor is likely to break; this pins the whole of what zap claims to do
# against the assembler it has to agree with, so a case nobody thought to write
# a unit test for still cannot drift.
echo "=== test_reference ==="
REF="$ROOT/test/ref/linux_x86_64/ez80asm"
if [ ! -x "$REF" ]; then
    echo "FAIL  vendored ez80asm missing at $REF"
    status=1
else
    for src in test/cases/*.s; do
        rm -f "$OUT/ref.bin" "$OUT/dz.bin"
        # Both are allowed to fail; a failure shows up as a missing or
        # differing output below. Without the guards `set -e` would end the
        # run at the first one and the cases after it would never be
        # reported at all.
        "$REF" "$src" "$OUT/ref.bin" > /dev/null 2>&1 || true
        # -ez80, because this comparison is the compatibility claim. zap's
        # default gives operators the precedence a reader expects and the
        # reference gives them none, so `1+2*3` is 7 by default and 9 here.
        # Running the default against the reference would be asking two
        # assemblers that disagree on purpose to agree.
        "$OUT/zap" -ez80 "$src" "$OUT/dz.bin" > /dev/null 2>&1 || true
        if [ -f "$OUT/ref.bin" ] && cmp -s "$OUT/ref.bin" "$OUT/dz.bin"; then
            echo "PASS  $(basename "$src") matches ez80asm"
        else
            echo "FAIL  $(basename "$src") differs from ez80asm"
            status=1
        fi
    done
fi

# Every character scan carries a bound.
#
# Unbounded, `while (is_space_ch(*p)) p++;` has compiled to a loop rotated the
# wrong way on the eZ80: the pointer is pre-decremented and each turn tests one
# character past it, so the first is never examined and the scan runs one past
# where it should. It has happened five times, on five different loops, and it
# reduces to nothing -- the same loop in isolation compiles correctly, and it
# has appeared and disappeared under changes that do not touch the loop at all.
# The last time, adding one function between `expr_value` and its callees
# rotated four loops in a function whose source was unchanged.
#
# **Nothing on the host reproduces it.** The host build is correct every time,
# which is why this is a check on the source and not on the behaviour: there is
# no input that fails here, and the only test that can bite before the Agon
# does is one that reads what was written.
#
# The rule is that a loop whose condition dereferences a pointer must also
# compare one of the pointers it dereferences against a limit. Two conditions
# are what stops the rotation. A loop that walks two pointers in step needs
# only the one bound -- `while (q != qend && *t == *q)` is bounded -- so any
# one of them satisfies it. A dereference does not count as its own bound,
# which is what makes `while (*p != '\n')` a failure and not a pass.
echo "=== test_scan_bounds ==="
unbounded=$(awk '
    {
        i = index($0, "while (")
        if (i == 0) { next }
        cond = substr($0, i + 6)
        n = 0
        rest = cond
        while ((j = index(rest, "*")) > 0) {
            rest = substr(rest, j + 1)
            sub(/^[ \t]+/, "", rest)
            if (match(rest, /^[A-Za-z_][A-Za-z_0-9]*/) == 0) { continue }
            n++
            names[n] = substr(rest, 1, RLENGTH)
        }
        if (n == 0) { next }
        bounded = 0
        for (k = 1; k <= n; k++) {
            if (cond ~ ("[^A-Za-z_0-9*]" names[k] "[ \t]*(<|!=|>)") \
                || cond ~ ("^" names[k] "[ \t]*(<|!=|>)")) { bounded = 1 }
        }
        if (!bounded) { printf "%s:%d: %s\n", FILENAME, FNR, $0 }
    }' src/*.c src/*.h)
if [ -z "$unbounded" ]; then
    echo "PASS  every character scan is bounded"
else
    echo "FAIL  an unbounded character scan can be rotated wrongly on the eZ80"
    printf '%s\n' "$unbounded" | sed 's/^/      /'
    status=1
fi

# The generated code, on the machine this is for.
#
# Some changes have no answer of their own: making a loop counter unsigned
# produces identical bytes and identical output, and the whole of it is that
# two signed ints compared with `<` cost a `call pe, __setflag` to repair the
# flags on overflow. The only place that is visible is the assembly, so that is
# where it is checked.
#
# Skipped, not failed, without the cross compiler -- the same way the reference
# comparison above is skipped without ez80asm.
echo "=== test_codegen ==="
if ! command -v agondev-config > /dev/null 2>&1 \
   && ! [ -x "$HOME/agondev/bin/agondev-config" ]; then
    echo "SKIP  no agondev toolchain; the target codegen is not checked"
else
    CC_EZ80="$HOME/agondev/bin/ez80-none-elf-clang"
    [ -x "$CC_EZ80" ] || CC_EZ80=ez80-none-elf-clang
    if "$CC_EZ80" -mllvm -z80-gas-style -mllvm -z80-print-zero-offset \
        -nostdinc -isystem "$HOME/agondev/include" -target ez80-none-elf \
        -DAGONDEV -Oz -Isrc -S -o "$OUT/zap.s" src/zap.c \
        > "$OUT/zap.s.log" 2>&1; then
        # The flag repairs left in the line assembler, as a detector for a
        # change that has no other signature -- not as a cost.
        #
        # `call pe, __setflag` is a *conditional* call, taken only when the
        # overflow flag is set, which comparing a token length against a small
        # constant never does. Removing eight of them measured 0.3% SLOWER, so
        # the count says nothing about speed and this number must not be
        # optimised for. What it does do is notice if the unsigned loop bound
        # in same_ci -- which measured 1.4% faster, for reasons that are not
        # this call -- is ever put back.
        nset=$(awk '/^_assemble_line:$/ { go = 1; next }
                    go && /^_[a-z_0-9]+:$/ { exit }
                    go && /call[ \t]+pe, __setflag/ { n++ }
                    END { print n + 0 }' "$OUT/zap.s")
        if [ "$nset" -le 12 ]; then
            echo "PASS  assemble_line has no more signed-compare repairs than it did ($nset)"
        else
            echo "FAIL  assemble_line has $nset signed-compare repairs, was 12"
            status=1
        fi

        # Every measuring configuration still builds.
        #
        # The truncation flags are how this project finds out where the time
        # goes, and they are compiled from paths the ordinary build does not
        # reach -- so an edit can break all of them and nothing notices until
        # somebody needs a measurement. That has happened: moving the state to
        # a fixed address left a `(void) z;` inside a `#if defined(TRUNC)`
        # block and every staged build failed for a fortnight of commits,
        # silently, because the runner only ever built the ordinary one.
        #
        # Compiled and not run. What is being asserted is that the code is
        # still valid, not that the numbers are still what they were.
        #
        # LTRUNC and PTRUNC name a stage inside a stage, so each is paired
        # with the TRUNC that reaches it; that is how they have always been
        # used and not a workaround.
        for flags in "-DTRUNC=1" "-DTRUNC=7" "-DTRUNC=4 -DTRUNC_NODIR" \
                     "-DTRUNC=4 -DTRUNC_NODIR -DMTRUNC" "-DTRUNC=3 -DLTRUNC=1" \
                     "-DTRUNC=5 -DPTRUNC=1" "-DETRUNC=1" "-DMTRUNC" \
                     "-DNOFIX" "-DEVAL=int" "-DZMALLOC"; do
            # shellcheck disable=SC2086
            if "$CC_EZ80" -mllvm -z80-gas-style -mllvm -z80-print-zero-offset \
                -nostdinc -isystem "$HOME/agondev/include" -target ez80-none-elf \
                -DAGONDEV -Oz -Isrc -S -o /dev/null $flags src/zap.c \
                > "$OUT/mflags.log" 2>&1; then
                echo "PASS  the measuring build $flags still compiles"
            else
                echo "FAIL  the measuring build $flags does not compile"
                sed 's/^/      /' "$OUT/mflags.log" | head -6
                status=1
            fi
        done

        # The rotated-scan shape, in the one function every line goes through.
        #
        # test_scan_bounds above says the source carries a bound. This says the
        # bound did its work, and it is the only place the fault is ever
        # visible: the host build is correct whether the loop is rotated or
        # not.
        #
        # The shape and not just `dec iy`. This counted the instruction alone
        # for one round and it was too blunt -- deleting an unnecessary
        # temporary moved the register allocation, `hex_digits`'s digit
        # counter landed in `iy`, and a loop that walks nothing at all read as
        # a rotated scan. What the fault actually looks like is a
        # pre-decremented pointer read one past:
        #
        #     dec  iy
        #   .LBB8_239:
        #     lea  bc, iy + 0
        #     inc  bc
        #     ld   e, (iy + 1)      <- the first character is never examined
        #     ...
        #     jr   nz, .LBB8_239
        #
        # So: a `dec iy` with a read of `(iy + 1)` close behind it. On the
        # commit before the scans were bounded that is 2, and a counter in
        # `iy` is 0 because nothing indexes through it.
        #
        # parse_operand is always_inline, so its scans are counted here too.
        ndec=$(awk '/^_assemble_line:$/ { go = 1; next }
                    go && /^_[a-z_0-9]+:$/ { exit }
                    !go { next }
                    /dec[ \t]+iy/ { watch = 12; next }
                    watch > 0 {
                        if ($0 ~ /\(iy \+ 1\)/) { n++; watch = 0; next }
                        watch--
                    }
                    END { print n + 0 }' "$OUT/zap.s")
        if [ "$ndec" = 0 ]; then
            echo "PASS  assemble_line has no rotated scan"
        else
            echo "FAIL  assemble_line has $ndec rotated scans; read the assembly"
            status=1
        fi
    else
        # A FAIL and not a SKIP. The toolchain is present -- the branch above
        # checked -- so this is the target build genuinely not building, and
        # that is the only place some things can be said at all: `int` is three
        # bytes there and four here, so a _Static_assert about a width fires
        # on one machine and is vacuous on the other. Reported as a skip, the
        # evaluator could be narrowed back below what DW32 needs and every
        # check here would still pass.
        echo "FAIL  the target build failed"
        sed 's/^/      /' "$OUT/zap.s.log" 2>/dev/null | head -20
        status=1
    fi
fi

# The generator's omission switches produce sources that assemble.
#
# attribute.sh is built on these, and a file that does not assemble reports as
# a missing figure rather than as a broken generator. One of them was broken
# exactly that way: with the conditionals left out, finish() still closed a
# block that had never been opened, and the stray ENDIF only appeared when the
# byte budget happened to end inside the window -- which 256 KiB does not and
# 32 KiB does. Four sizes, because the bug was a function of where the file
# stopped.
echo "=== test_isa_omit ==="
omit_bad=0
for sz in 32768 65536 131072; do
    for feat in equ macro cond assume suffix data; do
        ISA_OMIT="$feat" test/bench/gen_isa.sh real "$sz" > "$OUT/omit.s" 2>/dev/null
        if ! "$OUT/zap" -ez80 "$OUT/omit.s" "$OUT/omit.bin" > "$OUT/omit.log" 2>&1; then
            echo "      ISA_OMIT=$feat at $sz: $(tail -1 "$OUT/omit.log" | tr -d '\r')"
            omit_bad=1
        fi
    done
done
if [ "$omit_bad" = 0 ]; then
    echo "PASS  every ISA_OMIT source assembles, at three sizes"
else
    echo "FAIL  an ISA_OMIT source does not assemble"
    status=1
fi

# An expansion gets a scope of its own only when its body mentions a local,
# and the body that *names* one without defining one is the case that decides
# whether the test for it is right.
#
# Without the scope, `@here` would resolve against the caller and the file
# would assemble; with it, the body cannot see the caller's locals and the
# reference agrees -- "Unknown identifier '@here'" there. One `@` in the body
# is what asks for the scope, which is why a body that only refers to a local
# has to be checked as well as one that defines one.
printf 'g:\n@here:\n  nop\n  MACRO r\n  jp @here\n  ENDMACRO\n  r\n' \
    > "$OUT/mloc.s"
mloc=$("$OUT/zap" "$OUT/mloc.s" "$OUT/mloc.bin" 2>&1 | tr -d '\r' || true)
cli_check "a body naming a local it does not define is refused" \
    "$(printf '%s' "$mloc" | grep -c 'unknown label')" 1

# The macro machinery measurement rests on one property: the file with the
# invocations and the file with them written out must assemble to the *same
# bytes*. If they ever stop doing so, the number attribute.sh prints stops
# being the cost of macro_text, macro_run and a nested reader and becomes a
# comparison of two different programs -- and it would still print, and still
# look like a number.
#
# Small, because the property does not depend on the size, and a line count
# rather than a byte budget for the reason gen_isa.sh gives.
echo "=== test_macro_expansion ==="
ISA_LINES=600 test/bench/gen_isa.sh real > "$OUT/mw.s" 2>/dev/null
ISA_LINES=600 ISA_OMIT=macrocall test/bench/gen_isa.sh real > "$OUT/mc.s" 2>/dev/null
rm -f "$OUT/mw.bin" "$OUT/mc.bin"
"$OUT/zap" -ez80 "$OUT/mw.s" "$OUT/mw.bin" > /dev/null 2>&1 || true
"$OUT/zap" -ez80 "$OUT/mc.s" "$OUT/mc.bin" > /dev/null 2>&1 || true
ninv=$(grep -cE '^  (msave|mload|msum|mwait|mtri|mrest|mg)' "$OUT/mw.s" || true)
nexp=$(grep -cE '^  (msave|mload|msum|mwait|mtri|mrest|mg)' "$OUT/mc.s" || true)
if [ ! -f "$OUT/mw.bin" ] || [ ! -f "$OUT/mc.bin" ]; then
    echo "FAIL  one of the macro attribution sources does not assemble"
    status=1
elif [ "$ninv" -lt 5 ] || [ "$nexp" -ne 0 ]; then
    echo "FAIL  macrocall left $nexp invocations of $ninv"
    status=1
elif cmp -s "$OUT/mw.bin" "$OUT/mc.bin"; then
    echo "PASS  a macro written out assembles to the same bytes as its call"
else
    echo "FAIL  a macro written out assembles to different bytes from its call"
    status=1
fi

# The marginal-pricing flags in zap.c. The first three duplicate a table so
# the walk over it does twice the work; the rest duplicate a call to a function
# that is already out of line, so nothing is outlined by the measurement. Each one is only a measurement if the program
# still assembles the same bytes -- a flag that changed the output would price
# something other than the walk, and would do it invisibly, since the number it
# produced would still look like a number. Built and compared here against the
# same corpus the reference comparison uses, which is the largest input the
# host tests have.
echo "=== test_pricing_flags ==="
# The -ez80 flag itself, and that it is the only one taken.
printf '  ld hl, 1+2*3\n' > "$OUT/prec.s"
"$OUT/zap" "$OUT/prec.s" "$OUT/prec_def.bin" > /dev/null 2>&1 || true
"$OUT/zap" -ez80 "$OUT/prec.s" "$OUT/prec_ez.bin" > /dev/null 2>&1 || true
cli_check "default gives 1+2*3 the value 7" \
    "$(xxd -p "$OUT/prec_def.bin" 2>/dev/null | tr -d '\n')" "21070000"
cli_check "-ez80 gives 1+2*3 the value 9" \
    "$(xxd -p "$OUT/prec_ez.bin" 2>/dev/null | tr -d '\n')" "21090000"
"$OUT/zap" "$OUT/prec.s" "$OUT/prec2.bin" -ez80 > /dev/null 2>&1 || true
cli_check "the flag is taken after the filenames too" \
    "$(xxd -p "$OUT/prec2.bin" 2>/dev/null | tr -d '\n')" "21090000"
unk=$("$OUT/zap" -wat "$OUT/prec.s" "$OUT/x.bin" 2>&1 | tr -d '\r' || true)
cli_check "an unknown option is refused" \
    "$(printf '%s' "$unk" | grep -c 'Unknown option -wat')" 1

for flag in DUP_ROW DUP_GROUP DUP_BUCKET DUP_HASH DUP_SYMCHAIN DUP_INTERN DUP_LOCINTERN DUP_NUMTOK; do
    if ! cc "${CFLAGS[@]}" "-D$flag" -o "$OUT/zap_$flag" src/zap.c "${SRCS[@]}" \
         2>"$OUT/$flag.log"; then
        echo "FAIL  -D$flag does not build"
        status=1
        continue
    fi
    bad=0
    for src in test/cases/*.s; do
        rm -f "$OUT/base.bin" "$OUT/dup.bin"
        "$OUT/zap" "$src" "$OUT/base.bin" > /dev/null 2>&1 || true
        "$OUT/zap_$flag" "$src" "$OUT/dup.bin" > /dev/null 2>&1 || true
        cmp -s "$OUT/base.bin" "$OUT/dup.bin" || { bad=1; echo "      $(basename "$src")"; }
    done
    if [ "$bad" = 0 ]; then
        echo "PASS  -D$flag assembles identical bytes"
    else
        echo "FAIL  -D$flag changes the output, so it prices nothing"
        status=1
    fi
done

exit $status
