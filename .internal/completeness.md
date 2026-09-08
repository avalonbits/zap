# What zap still needs, measured

Taken by running the reference's own corpus -- `test/corpus`, the 247 sources
in scope -- through zap and classifying every divergence, then probing each
one against `ez80asm` to separate a missing feature from a user symbol that
merely looks like one.

    247 sources in scope        (Errors_cputype excluded, as it always is)
    115 byte-identical          81 before the mode suffixes, 98 before BLKB,
                                104 before the parser gaps, 110 before the
                                remaining directives
    119 rejected by both        the negative tests, working as intended --
                                106 before the error checks
     13 divergences             was 60

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
    ASCIZ  .ASCIZ            DONE -- the list, and then one zero
    FILLBYTE                 DONE, with one refusal; see below
    .RELOCATE  .ENDRELOCATE  DONE -- it is a second origin
    .CPU                     DONE -- a check, not a setting
    BLKL  DW32               four bytes, and the evaluator is three; see below

BLKB was not missing, it was wrong, which was worse: it was mapped to `DS`,
and the two are different directives.

    ds 2         at the end of a file    dropped        (reserve)
    ds 2, 0xAA   the fill is ignored     FF FF
    blkb 2       at the end of a file    FF FF          (emit)
    blkb 2, 0xAA                         AA AA

That was two of the three cases where both assemblers accepted a source and
the bytes differed. Fixed, with BLKW and BLKP, for six more identical sources.

FILLBYTE has one refusal. In the reference a reservation is a gap filled when
the file is written out, so the last FILLBYTE wins for every one of them,
backwards as well: `ds 2 / fillbyte 0xAA` fills that earlier reservation too.
One pass writes bytes where it meets them, and reproducing that means keeping
every reserved range to go back over -- 682 of them in isa_real, for a case
that appears nowhere in the corpus, where every FILLBYTE precedes what it
fills. A FILLBYTE that would change a reservation already written is refused.

## The 24-bit ceiling, which is one decision and not three

**BLKL and DW32 are both four bytes wide**, and the corpus fills them with
`0x55555555` and `-2147483648`. The expression evaluator works in the
machine's own word, 24 bits here, and that is a deliberate choice paid for on
every operand in every file. Implementing either means writing wrong bytes for
half of the reference's own cases, so both report an unknown instruction.

The same ceiling shows in a third place: `.relocate 0x1000000` is "Address
outside 24-bit range" in the reference and is accepted here, because the `0x`
fast path accumulates in the machine's word and truncates. The `$1000000`
spelling of the same number goes through the general parser and *is* caught.

Between them that is four corpus sources -- `compound_all_operator_values_dx`,
`compound_all_operator_values_blkx`, `Defines/compound`,
`Macro/argument_replacement_equ` -- plus one negative test. Widening the
evaluator is a performance question with a measurable answer, and nobody has
measured it yet. That is the decision, not the directives.

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

## 4. Error detection -- DONE

All thirteen. A label over 64 characters counting the `@`; an index
displacement outside -128..127, which was being emitted truncated and silently
wrong; a macro parameter that is a number, a mnemonic or a directive; a macro
defined twice; and an anonymous label in a macro body.

They cost isa_real 5.42s to 5.54s, and that is the frame rather than the
tests: the two on the label path each take assemble_line from 110 bytes to 113,
which is near the 128 an `ix` displacement reaches. Three placements were tried
and all read 113.

## What is left

Thirteen sources, and they are four things:

  - **The 24-bit evaluator**, above: five sources.
  - **`.cpu Z80` and `.cpu Z180`**, which ask for another machine's
    instruction set. Two sources, out of scope by the same rule that excludes
    Errors_cputype.
  - **Four whole programs** -- BBC BASIC, Rokky, and two of the Lessons --
    plus two Macro tests. Each needs looking at on its own; they are no longer
    failing for a reason the corpus has already named.

## Not missing

The instruction set as `zap/test/cases/opcodes.s` pins it, expressions and
both precedence modes, global, local and anonymous labels, EQU, ORG, DS, ALIGN,
DB, DW, DL, INCLUDE, INCBIN, ASSUME ADL, conditional assembly, and macros.

## The one thing that is not a feature

The output buffer is a single `realloc`'d array, so a large output cannot grow
on a 512 KB machine even when the source is small -- `DB "string"` at 256 KiB
fails part way through. The fix is to write output as it is produced and patch
fixups by seeking, which is a design change rather than a directive.
