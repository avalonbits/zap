# zap - eZ80 Assembler Project

A fast, single-pass assembler for the [Agon Light](https://www.thebyteattic.com/p/agon.html)
and other eZ80 machines. It runs **on** the Agon as well as on a desktop, and
it is a drop-in replacement for
[ez80asm](https://github.com/AgonPlatform/agon-ez80asm): the same command line,
the same syntax, and the same output bytes.

    $ zap hello.s hello.bin
    Assembling hello.s
    Wrote hello.bin, 42 bytes
    Done in 0.01 seconds

## Why

Assembling on the Agon itself is slow enough that most people cross-assemble on
a PC instead. zap is built to make on-machine assembly practical: on the
reference assembler's own test corpus it is **3.2x faster** per source and
**5.8x faster** on a real program (BBC BASIC for Agon: 3.9 seconds against
22.4). It produces byte-identical output, so switching costs nothing.

## Building

**For the Agon**, with the [agondev](https://github.com/AgonPlatform/agondev)
toolchain on your `PATH`:

    make                    # produces bin/zap.bin

Copy `bin/zap.bin` to `/bin` on the Agon's SD card and run it as `zap`.

**For the host**, to try it out or to run the test suite:

    cc -std=gnu11 -O2 -fsigned-char -include test/stubs/host_types.h \
       -Isrc -Itest/stubs -o zap src/*.c test/stubs/agon_stubs.c

## Usage

    zap <source> <output> [options]

    zap game.s game.bin                 # assemble
    zap game.s game.bin -l -s           # ... with a listing and a symbol file
    zap game.s game.bin -o 40000 -b 00  # origin 0x40000, fill byte 0x00
    zap game.s game.bin -w              # warn about truncated values

Options may be written with or without a space before their argument -- `-o
50000` and `-o50000` are the same thing -- and may appear before or after the
file names.

## Options

| Option | Meaning |
| --- | --- |
| `-v` | Print the version and assemble nothing |
| `-h` | List the options |
| `-o <hex>` | Origin address, in hex. Default `040000` |
| `-b <hex>` | Fill byte for reserved space, in hex. Default `FF` |
| `-a <0\|1>` | ADL mode. Default `1` |
| `-l` | Write a listing to `<source>.lst` |
| `-d` | Write the listing to the console instead |
| `-s` | Write the global symbols to `<source>.symbols` |
| `-x` | Print assembly statistics when finished |
| `-c` | No colour in messages |
| `-w` | Warn when a value does not fit where it is written |
| `-i` | Accepted for compatibility; the default already ignores those warnings |
| `-m` | Accepted for compatibility; zap has one memory configuration |
| `-ez80` | Use the reference assembler's expression rules (see below) |

## What it assembles

**Instructions.** The full eZ80 instruction set, including the `.SIS` `.LIS`
`.SIL` `.LIL` mode suffixes and their short spellings (`.S`, `.L`, `.IS`, and
so on).

**Labels.** Global labels, local labels scoped to the preceding global one
(`@name`), and anonymous labels (`@@` to define, `@f` and `@b` to refer
forwards and backwards).

**Expressions.** `+` `-` `*` `/` `<<` `>>` `&` `|` `^`, unary `-` and `~`,
parentheses, `$` for the current address, character literals with escapes, and
numbers written as `1234`, `0x1234`, `$1234`, `1234h`, `0b1010`, `%1010` or
`1010b`. `IF` takes an expression and treats a non-zero value as true.

**Directives.**

| | |
| --- | --- |
| Data | `DB` (`DEFB`, `BYTE`, `ASCII`), `DW` (`DEFW`), `DL` (`DW24`), `DW32`, `ASCIZ` |
| Space | `DS` (`DEFS`), `BLKB`, `BLKW`, `BLKP`, `BLKL`, `ALIGN`, `FILLBYTE` |
| Address | `ORG`, `.RELOCATE` / `.ENDRELOCATE`, `ASSUME ADL=` |
| Symbols | `EQU` |
| Files | `INCLUDE`, `INCBIN` |
| Conditional | `IF` / `ELSE` / `ENDIF` |
| Macros | `MACRO` / `ENDMACRO`, up to 8 parameters |
| Target | `.CPU EZ80`, `.CPU Z80`, `.CPU Z180` |

A leading `.` is optional on every directive, and they are case-insensitive.

`.CPU` selects an instruction set rather than a machine: the Z80 set includes
the undocumented instructions, and neither the Z80 nor the Z180 accepts ADL
mode or a mode suffix.

## Compatibility

zap is checked against ez80asm 2.2 on every source in the reference's own test
corpus. All 507 either assemble to identical bytes or are rejected by both
assemblers, as do BBC BASIC for Agon and Rokky with their whole include trees.

**`-ez80` reproduces three surprising behaviours of the reference**, which is
what the tests and benchmarks use:

* **No operator precedence.** `1+2*3` is 9, not 7 -- the expression is
  evaluated strictly left to right.
* **`IF` does not compare.** `IF a == b` evaluates `a` and discards the rest,
  so `IF 0 == 0` is false.
* **`0bh` is hexadecimal**, not binary: the `h` suffix is claimed before the
  `0b` prefix is considered.

Without `-ez80`, expressions behave the way a C programmer expects. Everything
else is the same in both modes.

## Diagnostics

An error names the file, the line and what went wrong, echoes the line, and for
a failure inside a macro also says where the macro was invoked:

    Macro [mos_call] in "kernel.s" line 12 - unknown label 'MOS_SYSVARS'
      ld a, MOS_SYSVARS
    Invoked from "main.s" line 84 as
      mos_call MOS_SYSVARS

With `-w`, a value too large for the space it is written into is reported and
the assembly continues:

    File "game.s" line 31 - Value truncated to 8 bit '0x100'

A value fits if the bytes that come out mean the same number read as signed or
as unsigned, so `ld a, -1` and `ld a, 255` are both fine while `ld a, 256` and
`ld a, -129` are not. The check is off by default because it costs about 2% of
a run; the reference has it on and cannot turn it off.

zap reports errors as codes internally, so it can be used as a library: the
message text lives in a table beside the enum rather than in the code that
detects the fault.

## Testing

    test/run.sh                 unit and CLI tests, plus every source in
                                test/cases assembled with both zap and the
                                reference and compared byte for byte
    test/corpus.sh              the reference's whole corpus and zap's own
                                regression sources, the same way
    test/corpus.sh --regress    just zap's own, in about two seconds
    test/bench/bench.sh         throughput against ez80asm on the emulator

The reference assembler and its corpus are vendored under `test/ref` and
`test/corpus`, so none of this needs a network. Benchmarks need
[fab-agon-emulator](https://github.com/tomm/fab-agon-emulator); everything else
runs on the host.

## Documentation

* [`docs/DESIGN.md`](docs/DESIGN.md) -- how the assembler works: the shape of a
  run, the one-pass design and what it costs, the symbol tables, the
  instruction table, macros, diagnostics, and what the eZ80 imposes on all of
  it.
* [`ez80_advanced_optimization_guide.md`](ez80_advanced_optimization_guide.md)
  -- writing fast C for the eZ80: the ordinary C that becomes library calls,
  what inlining actually does, two compiler bugs to know about, and how to
  measure any of it. Most of why zap is quick.

## License

GPL-3.0. The vendored reference assembler and its corpus under `test/` are MIT
licensed and belong to the [agon-ez80asm](https://github.com/AgonPlatform/agon-ez80asm)
project.
