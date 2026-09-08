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

Against the reference's own 247-source corpus, 98 assemble to identical bytes,
106 are refused by both, and 43 disagree. `test/corpus.sh` prints that table,
and `.internal/completeness.md` says what each of the 43 needs.

## What it supports

Every eZ80 instruction form in `test/cases/opcodes.s`, which is the reference's
own opcode corpus. Global, local (`@name`) and anonymous (`@@`, `@f`, `@b`)
labels. Expressions, in both precedence modes. `DB` `DW` `DL` `DS` `ALIGN`
`ORG` `EQU` `INCLUDE` `INCBIN` `ASSUME ADL` `IF`/`ELSE`/`ENDIF`
`MACRO`/`ENDMACRO`, and the `.SIS` `.LIS` `.SIL` `.LIL` instruction mode
suffixes with their short spellings.

## Testing

    test/run.sh        host tests: unit, CLI, and every source in test/cases
                       assembled against a vendored ez80asm and compared byte
                       for byte
    test/corpus.sh     the reference's whole corpus, the same way
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
