# zap

An assembler for the Agon Light, in one pass and one file.

It exists to agree with [ez80asm](https://github.com/AgonPlatform/agon-ez80asm)
byte for byte and to be fast enough that assembling on the machine itself is
not something to avoid.

    make                       an Agon build, bin/zap.bin
    zap [-ez80] source.s out.bin

## Compatibility

`-ez80` is the compatibility mode and is what the tests and benchmarks use. The
reference does several things that are surprising, and under `-ez80` zap does
them too rather than being right and incompatible:

  - **No operator precedence.** `1+2*3` is 9 there, and 9 here under `-ez80`.
  - **`IF` does not compare.** `IF a == b` evaluates `a` and throws the rest
    away, so `IF 0 == 0` is false.
  - **`0bh` is hexadecimal**, not binary: the `h` suffix is claimed before the
    `0b` prefix.

Without `-ez80` the defaults are the ones a reader expects. Where the two
disagree it is on purpose and it is written down; where they disagree by
accident it is a bug.

Against the reference's own 507-source corpus, **nothing disagrees**: 128
assemble to identical bytes and 379 are refused by both. So do BBC BASIC and
Rokky, assembled whole with their include trees.

`test/corpus.sh` prints that table, and runs 49 sources of zap's own beside
it -- `test/regress`, the cases a corpus written to test ez80asm could not
have. Those came from reading the reference's diagnostic table rather than
running its tests, and three of them were wrong bytes with nothing said.

## Options

The reference's, by the same letters and in the same forms -- `-o 50000` and
`-o50000` are the same thing there and are here, so a command line written for
it works unaltered.

    -v   version, and assemble nothing        -l   listing to <source>.lst
    -h   list the options                     -d   listing to the console
    -o   org start, hex, default 040000       -s   export <source>.symbols
    -b   fillbyte, hex, default FF            -x   assembly statistics
    -a   ADL mode 1/0, default 1              -c   no colour
    -w   warn about truncated values          -i   accepted, does nothing
    -m   accepted, does nothing
    -ez80  the reference's expression rules

`-o`, `-b` and `-a` change the bytes, and each is checked against the
reference by assembling the same source through both. `-m` is a memory
configuration and zap has one, the small one, so it is taken and does nothing.
`-i` asks for what is already true and does nothing too -- see below. An
option that is not the reference's is still refused.

`-w` is the one flag that is zap's own, and the one place the two command
lines mean different things rather than spelling the same thing differently.

## When something is nearly wrong

    $ zap prog.s prog.bin -w
    File "prog.s" line 1 - Value truncated to 8 bit '0x100'

A value that does not fit where it is written is said and the assembly carries
on, producing the bytes the reference produces. The line is drawn where the
reference draws it: a value fits if the bytes that come out mean the same
number read as signed or as unsigned, so `ld a, -1` and `ld a, 255` are both
fine and `ld a, 256` and `ld a, -129` are not.

**Unlike the reference, the check is off unless `-w` asks for it.** There, it
always runs and `-i` silences the message; here `-i` is accepted and does
nothing, because nothing is what the default already does. The reason is cost.
Every other diagnostic in zap is work done after something has gone wrong;
this one is a question asked of every value in every source, and asking it
costs 2% of a real program and 6.7% of a file that is nothing but immediates.
The reference's `-i` cannot give that back, because there the check runs
either way.

So a command line written for ez80asm still runs and still gets the same
bytes, and `-w` is there for when the question is worth 2%.

## When something is wrong

    Macro [m] in "prog.s" line 2 - unexpected text after the instruction
    ld a, 5 5
    Invoked from "prog.s" line 4 as
      m 5

The failing line, the token the message is about, and for a macro the place it
was invoked from. All of it is captured when the failure happens and none of
it is kept in advance, so **a source that assembles pays nothing for any of
it** -- measured, and the three benchmarks do not move by a hundredth.

Errors are `zap_err` codes rather than strings, so a caller that is not the
command line can branch on one; the text is in a table beside the enum.

## What it supports

Every eZ80 instruction form in `test/cases/opcodes.s`, which is the reference's
own opcode corpus. Global, local (`@name`) and anonymous (`@@`, `@f`, `@b`)
labels. Expressions, in both precedence modes. `DB` `DW` `DL` `DS` `ALIGN`
`ORG` `EQU` `INCLUDE` `INCBIN` `ASSUME ADL` `IF`/`ELSE`/`ENDIF`
`MACRO`/`ENDMACRO`, and the `.SIS` `.LIS` `.SIL` `.LIL` instruction mode
suffixes with their short spellings. `DW24` `DW32` `ASCIZ` `FILLBYTE`
`BLKB` `BLKW` `BLKP` `BLKL` `.RELOCATE`/`.ENDRELOCATE`.

`.CPU EZ80`, `.CPU Z80` and `.CPU Z180` select an instruction set, as they do
in the reference: the Z80 set includes the undocumented instructions, and
neither the Z80 nor the Z180 has ADL or a mode suffix.

## Testing

    test/run.sh        host tests: unit, CLI, and every source in test/cases
                       assembled against a vendored ez80asm and compared byte
                       for byte
    test/corpus.sh     the reference's whole corpus, the same way, plus
                       zap's own sources in test/regress
    test/corpus.sh --regress    only those, in about two seconds
    test/bench/bench.sh    throughput against ez80asm on fab-agon-emulator

The reference assembler and its corpus are vendored under `test/ref` and
`test/corpus`, both MIT licensed, so none of the above needs a network.

## How it is built

One pass, because a second one over 256 KiB of source on a 512 KB machine is
time nobody has. A reference to a label further down cannot be resolved where
it is read, so the output is held in memory and patched at the end.

One translation unit, and not for the usual reasons: the eZ80 has no cache and
a call is expensive, so what matters is how much of the hot path the compiler
can see at once. `assemble_line` has the operand parser, the row match and the
emitter inlined into it. Splitting it would be tidier and slower, and that has
been measured more than once.

It began as `dzap`, a stripped-down assembler written to find out how much of
the earlier zap's 830 cycles per source byte was assembling and how much was
machinery. Every feature was then added back one at a time and priced on the
Agon before the next one started; it reached the same feature set for a third
of the cycles and replaced what it was measuring. The measurements, including
the ones that were tried and put back, are in `.internal/performance-notes.md`.
