# What zap still needs, measured

Taken by running the reference's own corpus -- `test/corpus`, the 247 sources
in scope -- through zap and classifying every divergence, then probing each
one against `ez80asm` to separate a missing feature from a user symbol that
merely looks like one.

    247 sources in scope        (Errors_cputype excluded, as it always is)
    110 byte-identical          81 before the mode suffixes, 98 before BLKB,
                                104 before the parser gaps
    106 rejected by both        the negative tests, working as intended
     31 divergences             was 60

The 60 are what follows. They are ranked by what they unblock, not by how many
tests they fix, because those two orders are very different here.

## 1. Instruction mode suffixes -- `.SIS` `.LIS` `.SIL` `.LIL`  -- DONE

Landed. Seventeen more sources are byte-identical, fifteen of them whole
programs, and the four benchmarks did not move by a hundredth of a second.
What the section said before it was done:

**This one feature is the only thing standing between zap and every real
program in the corpus.** BBC BASIC, Rokky and all nineteen AgonBits Lessons
programs fail on it and on nothing else:

    Z_PRG_Agon-Rokky        crt.inc line 112:      rst.lil $10
    Z_PRG_Agon-bbc-basic-v  agon_init.inc line 30: RST.LIS 08h
    Z_PRG_AgonBits_Lessons  nineteen of them, all inside MOSCALL, which is
                            a macro whose body is `LD A, function / RST.LIL $08`

About 240 uses across the corpus, `rst.lil` alone accounting for 115. A suffix
puts one prefix byte in front of the instruction and overrides the ADL mode for
it, which changes the width of the operand as well:

    ld.sis hl, 0x1234    40 21 34 12        .SIS  0x40
    ld.lis hl, 0x1234    49 21 34 12        .LIS  0x49
    ld.sil hl, 0x1234    52 21 34 12 00     .SIL  0x52
    ld.lil hl, 0x1234    5B 21 34 12 00     .LIL  0x5B

The short spellings `.S` and `.L` are in the corpus too -- `jp.l`, `ret.l`,
`reti.l`, `jp.s` -- and so is `ld.pis`, which the reference accepts.

The width override is the substantive part: the suffix has to reach the row
match and the emitter, not just add a byte.

**What it left behind.** Rokky and three of the Lessons now assemble and
disagree on bytes rather than failing outright, and BBC BASIC gets from line
30 of its init file to line 245. Three sources now stop at "bad displacement",
which nothing reached before. Those are the new frontier and are not in the
tiers below, which were written when the suffixes hid them.

## 2. Directives

    BLKB  BLKW  BLKP         DONE -- n units of a given fill, emitted
    BLKL                     four bytes, and the evaluator is three; see below
    DW32  .DW32              four-byte data
    ASCIZ  .ASCIZ            a string with a terminator
    FILLBYTE                 sets what a reservation is filled with
    .RELOCATE  .ENDRELOCATE  a relocatable block
    .CPU                     two in-scope uses; see below

BLKB was not missing, it was wrong, which was worse: it was mapped to `DS`,
and the two are different directives.

    ds 2         at the end of a file    dropped        (reserve)
    ds 2, 0xAA   the fill is ignored     FF FF
    blkb 2       at the end of a file    FF FF          (emit)
    blkb 2, 0xAA                         AA AA

That was two of the three cases where both assemblers accepted a source and
the bytes differed. Fixed, with BLKW and BLKP, for six more identical sources.

**BLKL is the one that is not just work.** It is four bytes wide and the
corpus fills it with `0x55555555` and `-2147483648`; the expression evaluator
works in the machine's own word, which is 24 bits here, and that is a
deliberate choice paid for on every operand in every file. Implementing BLKL
means either writing wrong bytes for half of the reference's own cases or
widening the evaluator, which is a performance decision and not a directive.
It reports an unknown instruction until someone makes that decision.

`.CPU` is 262 uses in the corpus and 260 of them are in `Errors_cputype`, which
is out of scope because zap is eZ80-only. The other two are `Opcodes/z180_new`
and `Opcodes/z80_undocumented`, and the honest handling is to accept `.cpu
ez80` and refuse the rest, rather than refuse the line and take the whole file
down with it.

## 3. Parser gaps -- DONE

All five, for six more identical sources. What they were:

  - Character-literal escapes. `LD A, '\a'` was "expected a character" against
    3E 07, and `'\''` could not be written at all. The backslash turned out to
    be both its own escape and itself: `'\'` is 5C and `'\''` is 27, and only
    the fourth character tells them apart.
  - The `\?` string escape, which the reference takes. `\0` is still refused
    by both.
  - `ASSUME ADL = <expression>`, which the reference's own Labels corpus
    writes.
  - A conditional inside an included file. The rule is per file, not per
    assembly -- and an IF left open at the end of one is an error either way.
  - A MACRO definition inside a branch that is not taken, which was captured
    rather than skipped. The skip path asks `kind >= DIR_IF` and nothing
    else, so the fix was to put MACRO and ENDMACRO below the conditionals in
    the numbering.

## 4. Error detection, where zap is too permissive

Thirteen negative tests that the reference refuses and zap accepts. None of
them changes the bytes of a valid program, and all of them are part of being
finished:

    Errors_labels    label maximum length, global and local
    Errors_macros    argument name rules (six), an anonymous label in a body,
                     a macro whose name is already defined
    Errors_opcodes   relative displacement at the positive and negative limits

## Not missing

The instruction set as `zap/test/cases/opcodes.s` pins it, expressions and
both precedence modes, global, local and anonymous labels, EQU, ORG, DS, ALIGN,
DB, DW, DL, INCLUDE, INCBIN, ASSUME ADL, conditional assembly, and macros.

## The one thing that is not a feature

The output buffer is a single `realloc`'d array, so a large output cannot grow
on a 512 KB machine even when the source is small -- `DB "string"` at 256 KiB
fails part way through. The fix is to write output as it is produced and patch
fixups by seeking, which is a design change rather than a directive.
