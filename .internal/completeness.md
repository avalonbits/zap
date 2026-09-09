# What zap still needs, measured

Taken by running the reference's own corpus -- `test/corpus`, the 247 sources
in scope -- through zap and classifying every divergence, then probing each
one against `ez80asm` to separate a missing feature from a user symbol that
merely looks like one.

    247 sources in scope        (Errors_cputype excluded, as it always is)
    122 byte-identical          81 before the mode suffixes, 98 before BLKB,
                                104 before the parser gaps, 110 before the
                                remaining directives, 115 before two bugs the
                                Macro tests were sitting on, 119 before Rokky,
                                120 before BBC BASIC, 121 before a BLK fill
                                that names something ahead
    119 rejected by both        the negative tests, working as intended --
                                106 before the error checks
      6 divergences             was 60

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

That third one came out with the other two and was not aimed at. The test was
`value > 0xFFFFFF`, which a 24-bit int cannot satisfy -- **dead code on the
Agon and live on the host**, which is why only one of the two spellings ever
failed. Both are refused now, and test/run.sh checks both.

Between them that is four corpus sources -- `compound_all_operator_values_dx`,
`compound_all_operator_values_blkx`, `Defines/compound`,
`Macro/argument_replacement_equ` -- plus one negative test. Widening the
evaluator is a performance question with a measurable answer, and nobody has
measured it yet. That is the decision, not the directives.

**Since resolved.** The answer was 1.8%, both directives are in, and all four
sources are identical. See section 5.

`.CPU` is 262 uses in the corpus and 260 of them are in `Errors_cputype`, which
is out of scope because zap is eZ80-only. The other two are `Opcodes/z180_new`
and `Opcodes/z80_undocumented`, and the honest handling is to accept `.cpu
ez80` and refuse the rest, rather than refuse the line and take the whole file
down with it.

**Since resolved, and the paragraph above is wrong twice.** See section 6.

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

## 5. The evaluator's width, DW32 and BLKL -- DONE

Four sources, and one number, arrived at in three steps across three rounds.

It was recorded as a decision with a measurable answer nobody had measured.
The first attempt to measure it produced a build that **did not assemble**:
`in0 a, (5)`, which does not touch the evaluator, came out as "unexpected text
after the instruction" on the Agon and correctly on the host, because adding
one function between `expr_value` and the two it calls moved the register
allocation and rotated four of `assemble_line`'s unbounded scans. That was the
rotation being measured, not the width.

Bounding every scan removed it, for 0.06s. Then the width could be asked
properly, and the answer is **1.8%** -- 5.64s to 5.74s on isa_real, after
three rounds of getting it down from the naive widening's 7.4%.

What is wide: an expression, and a symbol's value. What is not: an address, a
count, an immediate, a displacement, and a fixup's addend. The reference keeps
32 bits in a label and truncates at the *emitter*, on the width the directive
asked for, and that is now reproduced exactly.

`DW32` and `BLKL` came with it, because they are the same change: they are
four bytes wide and no care at the emitter recovers a bit the evaluator has
already dropped.

The full decomposition -- including the two things that measured and were not
kept, and a new way for the Agon and the host to disagree about correct C --
is in .internal/performance-notes.md.

## 6. `.CPU`, and a scope line that should never have been drawn

The last two sources, and 260 more that the runner was not even looking at.

**The reasoning that put them out of scope was wrong, and it was wrong about
this repository.** It said reproducing the reference's CPU filter would be a
second assembler's worth of rows. Every row in `src/isa_table.c` has carried a
CPU mask since the day it was written -- `BIT_Z80`, `BIT_U80` for the
undocumented Z80 forms, `BIT_Z180`, `BIT_EZ80` -- and `match_row` has always
tested it, against a hard-coded `CPU_EZ80`. The rows were there. Refusing the
directive was the only thing in front of them.

Two things follow from that, and the second matters more than the first.

The first is the count. `Opcodes/z180_new` is byte-identical on the directive
alone -- one line of work for a source called out of scope. `z80_undocumented`
needed three real pieces: the three-operand `RES n,(IX+d),r`, a transform bug
in twenty-four rows that had never been reachable, and the CPU-dependent
refusals for ADL and the mode suffixes. `Errors_cputype`'s 260 sources needed
**nothing but the filter** in 250 cases.

The second is what "out of scope" was doing. It was standing in for "I have
not looked", and it had been standing there long enough to be quoted in a PR
body as though it were a decision. A scope line is a claim about the work, and
a claim about the work has to be checked like any other. The reference
assembles both of those files; zap says it is a drop-in replacement for the
reference; that is the whole argument and there was never a counter to it.

**A behaviour the reference has is not a candidate for `-ez80`, either.** That
flag is for the places the reference is *wrong* and zap reproduces it anyway --
no operator precedence, `IF a == b` discarding the comparison. `.CPU` is not a
quirk to hide behind a flag; it is a feature to have.

What it cost: 0.3% on isa_real, 1.5% on isa_degenerate. See
.internal/performance-notes.md.

## What is left

**Nothing in the corpus.** Every source in the reference's corpus -- all 507 of them, with
`Errors_cputype` no longer skipped -- either assembles to identical bytes or
is refused by both assemblers.
Every whole program in the corpus now assembles byte-identically. The last two
were Rokky, whose bug was a global minus a local resolving against the wrong
local -- three bytes in 31,520 with both assemblers accepting the file -- and
BBC BASIC, which needed an index displacement to be an expression and an
expression to be kept as text when a fixup could not hold it.

## Where these are pinned

Every divergence in this section has a source in `test/regress` that fails if
it comes back -- zap's own tree, run by test/corpus.sh beside the vendored
corpus, 49 sources. Each was checked by reverting the fix and watching the
case turn red. What that tree cannot see is written in its README: a
difference that is only in a message, and anything that depends on a
command-line flag. test/run.sh covers both.

## What the corpus could not see, and what it cost to look

The corpus is 507 sources of valid code and negative tests, so it can only
find a divergence somebody already wrote down. The reference's own diagnostic
table -- 80 error codes and 5 warnings, readable with `strings` -- says what
it checks, and each one probed against both assemblers found eight things the
corpus never could.

**Wrong bytes, silently, and this is the class that matters.** An operand that
folds into the opcode was masked rather than checked:

    bit 8, a     assembled as bit 0, a
    im 3         assembled as im 0
    rst 0x09     assembled as rst 08h -- a working call to the wrong vector

and a fold whose value was still ahead of it was dropped altogether, so
`bit n, a` with `n` an EQU further down the file was `bit 0, a` on legal code
that the reference gets right. Three fixup widths above the byte counts now
say come back to the opcode byte. The reference's rules here are odd and are
reproduced as they are: a bit number above 7 is refused, one below 0 is
shifted straight in, so `bit -1, a` is CB FF in both.

**Taken where the reference refuses.** An ORG outside 16 bits with ADL 0. A
line longer than 256 characters -- the reference counts a CR, so a CRLF file
gets 255, and zap's limit was the 16 KB reader buffer. A macro name longer
than 64 characters, where zap had no limit at all.

**Said where the reference says something.** `ds 4, 0xAA` reserves four bytes
and drops the 0xAA; the reference says so and zap did not. That was the one
warning of its five zap had no answer for. It is not behind `-w`, and the
reference draws the same line: `-i` does not silence it there either.

**Said where the reference says nothing, on one machine only.** zap warned
that `ld hl, 0x12345678` was truncated to 24 bits. The reference does not --
it checks a *directive* against 24 bits and an instruction's immediate never
-- and worse, the check was written so that it folded away on the Agon and
survived on the host. The host warned and the target did not, and the target
was the one that was right. A check that only fires on the machine the tests
run on is worse than not having it.

## The one place a difference is deliberate: a negative reservation

`ds -1` on its own assembles cleanly in the reference and writes nothing, so
it looks like something zap refuses needlessly. Put one byte after it and the
same source writes **4,294,967,299 bytes** -- the count is read as unsigned
and the gap is filled on the way out. `blkb -2`, which emits rather than
reserves, is 3.5 GB with nothing after it at all.

There is no byte sequence there worth agreeing with on a 512 KB machine, so
both are refused. It is the same position as division by zero.

## The listing, and the two things one pass cannot do

An expansion is now listed the way the reference lists it -- the invocation
with no bytes on it, the arguments under it, and a line per body line
carrying the bytes that line wrote and the depth it wrote them at -- and the
file is the reference's bytes, LF-terminated with one stray CR after the
header. A listing of a source with no macros is byte-identical to the
reference's and run.sh compares them.

Four differences are left and all four are the same wall: the reference lists
on its second pass and knows everything before it writes line 1, while zap
writes each line as it assembles it.

**A forward reference is listed with the bytes as they were emitted, not as
they were patched.** `ld hl, ahead` is `21 00 00 00` here and `21 17 00 04`
there. This one is not about macros at all and reaches every listing of every
real program; it was found by comparing .lst files in the corpus runner, which
is what that comparison is for. Closing it means either buffering the listing
or recording a file offset per fixup and seeking back to rewrite twelve
characters -- and `-d`, which goes to the console, could not be fixed either
way.

**A reservation's fill is listed differently again.** `ds 4` there leaves the
first row's byte field empty and puts the fill on a continuation row, unpadded;
`align 4` does the same; an ORG's pad is listed inline and padded, which zap
matches. zap lists all three inline. And a reservation at the end of a file,
which both assemblers drop, is listed with its bytes here and with none there.

**The reference widens the line-number column by two characters for the whole
file when any expansion is listed.** It can do that because it lists on the
second of two passes and knows before it writes line 1. zap writes each line
as it assembles it, and a file that expands its first macro on line 500 has
499 lines already written. The alternatives are to buffer the listing --
400 KB for BBC BASIC, on a machine with 512 -- or to rewrite the file at the
end, and neither is worth a two-space column for a debugging aid.

**A body line loses the indentation it was written with**, because a macro
body is stored from its first token and the marks that find its parameters
are offsets into that. The listing shows `db x` where the reference shows
`  db x`.

The console listing keeps CRLF rather than the reference's bare LF, which is
a fifth difference and the only deliberate one: `-d` there staircases down an
Agon screen.

The `listing/` group in test/regress is the sources that avoid all four
structural ones, so their `.lst` can be compared byte for byte. That
comparison is what caught zap writing CRLF where the reference writes LF, and
what found the forward-reference difference above.

## The one warning the reference has, and the one place zap does not copy it

Value truncation, now emitted -- with `-w`:

    File "prog.s" line 1 - Value truncated to 8 bit '0x100'

Same rule as the reference -- a value fits `width` bytes if it lies in
`-2^(8w-1) .. 2^(8w)-1`, so `ld a, -1` and `ld a, 255` both pass and
`ld a, 256` and `ld a, -129` do not -- and all seven boundaries are checked
against it in test/run.sh, zap with `-w` against the reference with nothing.

**The default differs deliberately, and it is the only flag that does.** There
the check always runs and `-i` silences the message; here the check is off and
`-w` turns it on. `-i` is still accepted, and now says something true: the
default already ignores them.

The reason is that the reference's `-i` does not buy back what the check
costs, because there the check runs either way -- and here it costs 2.1% of
bbcbasic and 6.7% of isa_real. Every other diagnostic in the program is work
done after a source has already gone wrong. This is the only one that asks a
question of every source that has not.

A command line written for ez80asm therefore still runs, still produces the
same bytes, and differs only in whether a message it did not ask for appears.

Two differences in the text as well, both deliberate:

* **The reference quotes the source token; zap prints the value.** `ld a, 256`
  reads `'256'` there and `'0x100'` here. Quoting the token means holding a
  pointer and a length for every operand of every line, all the way down to
  the emitter -- a cost paid by every source that has nothing wrong with it,
  to improve a message that only prints when something does. The same reason
  there is no echoed source line.
* Neither changes a byte of output, and neither changes the exit status: the
  reference exits 0 on a truncation and so does zap.

What it costs is in .internal/performance-notes.md, and it is the most
expensive diagnostic in the program -- 2% of a real program, 6.7% of a file
that is nothing but immediates -- because it is the only one that asks a
question of every source rather than doing work after one has gone wrong.

## One divergence found while testing the macro marks

**The reference substitutes a parameter name found inside a longer
identifier.** zap substitutes whole identifiers only.

    aa: EQU 6
      MACRO m a
      db aa
      ENDMACRO
      m 5

The reference reports `Unknown identifier 'a5'` -- it replaced the `a` at the
end of `aa` and left the `a` in front of it. zap assembles `db aa` as 6,
because `aa` is a whole identifier and is not the parameter `a`.

Not in the corpus, which is why 507 sources agree without it, and not
introduced by anything recent: matching whole identifiers is what the
substitution has always done. It is written down here rather than reproduced,
because reproducing it means substituting inside names -- the C preprocessor
hazard, in an assembler -- and a program that relies on it is a program whose
author did not mean it. If a real source ever needs it, this is the note that
says the behaviour was known and the decision was deliberate.

## One refusal that is left, and is not in the corpus

`@local - global`, with both still ahead, is "unknown label" here. The mirror
of it -- `global - @local` -- was Rokky's bug and is fixed; this one puts the
fixup on the *local* list, because that is where its target belongs, and the
global half is not known when the scope ends. Settling it would mean the
fixup carrying "negate the target", and the width byte has no bit spare.

It is a refusal rather than wrong bytes, no source in the corpus writes it,
and it read the same before the Rokky fix as after.

## Not missing

The instruction set as `zap/test/cases/opcodes.s` pins it, expressions and
both precedence modes, global, local and anonymous labels, EQU, ORG, DS, ALIGN,
DB, DW, DL, INCLUDE, INCBIN, ASSUME ADL, conditional assembly, and macros.

## The one thing that is not a feature

The output buffer is a single `realloc`'d array, so a large output cannot grow
on a 512 KB machine even when the source is small -- `DB "string"` at 256 KiB
fails part way through. The fix is to write output as it is produced and patch
fixups by seeking, which is a design change rather than a directive.
