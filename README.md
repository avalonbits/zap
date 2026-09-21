# zap - eZ80 Assembler Project

[![latest release](https://img.shields.io/github/v/release/avalonbits/zap?label=download&color=blue)](https://github.com/avalonbits/zap/releases/latest)

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

Assembling on the Agon itself used to be slow enough that most people
cross-assembled on a PC instead: ez80asm 2.2 took 22 seconds over BBC BASIC for
Agon, and zap was written to find out how much of that was the machine and how
much was the design. It turned out to be mostly the design — one pass instead
of two, fixups patched into the output, and C written for a chip with no cache
and a three-byte word — and zap came out four to six times faster.

**That gap has closed, and closing it was the point.** ez80asm 2.3 converted to
a one-pass design with fixups and a memory/file window of its own, and took
several of zap's optimizations with it. Measured on the same emulated Agon,
each assembler's own `Done in` line:

| source | zap | ez80asm 2.3 | |
|---|---|---|---|
| BBC BASIC for Agon, 386 KB of source | **3.98s** | 5.86s | 1.5x |
| Rokky | **0.54s** | 0.84s | 1.6x |
| 471 KB of straight instructions | **7.32s** | 13.00s | 1.8x |
| 256 KB of output | **0.72s** | 6.36s | 8.8x |
| 1 MiB of output | **2.86s** | 25.32s | 8.9x |

Byte-identical output in all five. The last two rows are where the designs
still differ: zap writes the output through a 64 KB window as it assembles, so
what it can produce is bounded by the card rather than by the machine, and the
only ceiling left is the eZ80's own 24-bit addressing at 8 MB. Read those two
rows as a ratio and not as a clock — the emulator charges nothing for writing
to the card, and a real Agon does; the same 1 MiB takes 15.2s on hardware.

Two assemblers that agree byte for byte are worth more than one. Each is a
check on the other, which is worth more to anyone writing eZ80 than either of
them being alone and unverifiable.

## Getting it

**From the [releases page](https://github.com/avalonbits/zap/releases) --
this is the way to get zap.** Every release has `zap.bin`, already built for
the Agon; there is nothing to compile, install or configure.

1. Download
   [`zap.bin`](https://github.com/avalonbits/zap/releases/latest/download/zap.bin)
   from the [latest release](https://github.com/avalonbits/zap/releases/latest).
2. Copy it into `/bin` on the Agon's SD card.
3. Run it: `zap hello.s hello.bin`.

`zap -v` prints the version, so you can check which one you have. The current
release is [v1.1.0](https://github.com/avalonbits/zap/releases/tag/v1.1.0).

## Building it yourself

**For the Agon**, with the [agondev](https://github.com/AgonPlatform/agondev)
toolchain on your `PATH`:

    make                    # produces bin/zap.bin

Copy `bin/zap.bin` to `/bin` on the Agon's SD card and run it as `zap`.

**For the host**, to try it out or to run the test suite:

    cc -std=gnu11 -O2 -fsigned-char -include test/stubs/host_types.h \
       -Isrc -Itest/stubs -o zap src/*.c test/stubs/agon_stubs.c

## Usage

    zap <source> [<output>] [options]

    zap game.s game.bin                 # assemble
    zap game.s                          # ... naming game.bin after the source
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
| `-m` | Accepted for compatibility; zap has one memory configuration, and it does not depend on the size of the output |
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

zap is checked against **ez80asm 2.2** on every source in the reference's own
test corpus. All 507 either assemble to identical bytes or are rejected by both
assemblers, as do BBC BASIC for Agon and Rokky with their whole include trees.
That is the binary vendored under `test/ref`, and it is the definition zap is
written to.

**ez80asm 2.3 changed three of those behaviours**, and zap still follows 2.2.
All three are places where 2.2 reproduced something accidental and 2.3, being
one-pass now, does not:

* a **bit number defined later** and out of range — `bit n, a` with `n: EQU -1`
  — is masked to `bit 7, a` by 2.2 and refused by 2.3;
* an **index displacement defined later** is held in sixteen bits by 2.2, so
  `(ix+0x40018)` is offset 0x18; 2.3 refuses it as out of range;
* **space reserved before the file's first `FILLBYTE`** takes the file's *last*
  fill byte in 2.2 and 0xFF in 2.3.

Everything else in the corpus still agrees byte for byte with 2.3, including
every instruction form and both real programs. Which of the two to follow is a
decision, not an oversight; the differences are listed here so that anyone
comparing output against 2.3 knows where to look.

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
    ZAP_WINDOW=512 test/corpus.sh
                                the same, with the output window forced small
                                enough that every source is written out in
                                pieces and patched behind
    FIX_CAP=1024 test/corpus.sh
                                and again with the fixup list capped, so that
                                the sweep which settles what it can when the
                                list will not grow runs on every source
    test/window.sh              a generated source several windows wide, with
                                every kind of fixup settled long after the
                                bytes holding it were written
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
