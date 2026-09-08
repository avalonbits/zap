# What dzap still needs, measured

Taken by running the reference's own corpus -- `test/corpus`, the 247 sources
in scope -- through dzap and classifying every divergence, then probing each
one against `ez80asm` to separate a missing feature from a user symbol that
merely looks like one.

    247 sources in scope        (Errors_cputype excluded, as it always is)
     81 byte-identical
    106 rejected by both        the negative tests, working as intended
     60 divergences

The 60 are what follows. They are ranked by what they unblock, not by how many
tests they fix, because those two orders are very different here.

## 1. Instruction mode suffixes -- `.SIS` `.LIS` `.SIL` `.LIL`

**This one feature is the only thing standing between dzap and every real
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

## 2. Directives

    BLKB  BLKW  BLKL  BLKP    reserve n units of a given fill, and *emit* them
    DW32  .DW32              four-byte data
    ASCIZ  .ASCIZ            a string with a terminator
    FILLBYTE                 sets what a reservation is filled with
    .RELOCATE  .ENDRELOCATE  a relocatable block
    .CPU                     two in-scope uses; see below

**BLKB is not missing, it is wrong**, which is worse. dzap maps it to `DS`, and
the two are different directives:

    ds 2         at the end of a file    dropped        (reserve)
    ds 2, 0xAA   the fill is ignored     FF FF
    blkb 2       at the end of a file    FF FF          (emit)
    blkb 2, 0xAA                         AA AA

That is two of the three cases where both assemblers accept a source and the
bytes differ. `blkw`, `blkl` and `blkp` are the two-, four- and three-byte
versions of the same thing.

`.CPU` is 262 uses in the corpus and 260 of them are in `Errors_cputype`, which
is out of scope because zap is eZ80-only. The other two are `Opcodes/z180_new`
and `Opcodes/z80_undocumented`, and the honest handling is to accept `.cpu
ez80` and refuse the rest, rather than refuse the line and take the whole file
down with it.

## 3. Parser gaps

  - **Character-literal escapes.** `LD A, '\a'` is 3E 07 in the reference and
    "expected a character" here. So is `'\''`.
  - **The `\?` string escape.** The reference takes it; dzap does not. `\0` is
    refused by both, which the comment in `str_escape` already says.
  - **`ASSUME ADL = <expression>`.** dzap wants a literal 0 or 1;
    `assume adl=before` where `before` is an EQU assembles in the reference.
  - **A conditional inside an included file** while the caller has one open.
    dzap says "conditionals do not nest". The reference allows it -- the rule
    is per file, not per assembly.
  - **MACRO inside a switched-off branch is still captured.** `.if 1 / macro
    test / .db 0 / endmacro / .else / macro test / .db 1 / endmacro / .endif`
    gives 00 in the reference and 01 here: the definition in the branch that
    was not taken overwrote the one that was. The third byte divergence.

## 4. Error detection, where dzap is too permissive

Thirteen negative tests that the reference refuses and dzap accepts. None of
them changes the bytes of a valid program, and all of them are part of being
finished:

    Errors_labels    label maximum length, global and local
    Errors_macros    argument name rules (six), an anonymous label in a body,
                     a macro whose name is already defined
    Errors_opcodes   relative displacement at the positive and negative limits

## Not missing

The instruction set as `dzap/test/cases/opcodes.s` pins it, expressions and
both precedence modes, global, local and anonymous labels, EQU, ORG, DS, ALIGN,
DB, DW, DL, INCLUDE, INCBIN, ASSUME ADL, conditional assembly, and macros.

## The one thing that is not a feature

The output buffer is a single `realloc`'d array, so a large output cannot grow
on a 512 KB machine even when the source is small -- `DB "string"` at 256 KiB
fails part way through. The fix is to write output as it is produced and patch
fixups by seeking, which is a design change rather than a directive.
