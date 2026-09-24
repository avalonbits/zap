# zap - eZ80 Assembler Project

[![latest release](https://img.shields.io/github/v/release/avalonbits/zap?label=download&color=blue)](https://github.com/avalonbits/zap/releases/latest)

zap is a fast, single-pass assembler for the [Agon Light](https://www.thebyteattic.com/p/agon.html)
and other eZ80 machines. It runs on the Agon itself as well as on a desktop,
and it's a drop-in replacement for
[ez80asm](https://github.com/AgonPlatform/agon-ez80asm): same command line,
same syntax, same output bytes.

    $ zap hello.s hello.bin
    Assembling hello.s
    Wrote hello.bin, 42 bytes
    Done in 0.01 seconds

## Why

Assembling on the Agon used to be slow enough that most people cross-assembled
on a PC. ez80asm 2.2 took 22 seconds to build BBC BASIC for Agon, and I wanted
to know how much of that was the machine and how much was the design. Mostly
the design, it turned out: one pass instead of two, fixups patched into the
output, and C written for a chip with no cache and a three-byte word. zap came
out four to six times faster.

Since then ez80asm 2.3 switched to a one-pass design as well and picked up
several of zap's optimizations, so the gap is much smaller now. Timed on the
same emulated Agon:

| source | zap | ez80asm 2.3 | |
|---|---|---|---|
| BBC BASIC for Agon, 386 KB of source | **3.98s** | 5.86s | 1.5x |
| Rokky | **0.54s** | 0.84s | 1.6x |
| 471 KB of straight instructions | **7.26s** | 13.00s | 1.8x |
| 256 KB of output | **0.72s** | 6.36s | 8.8x |
| 1 MiB of output | **2.86s** | 25.32s | 8.9x |

The output was byte-identical in every case. The last two rows are where the
designs still differ: zap streams its output through a 64 KB window, so the
size of what it can produce is limited by the SD card rather than by RAM (up to
the eZ80's 8 MB address space). Treat those two as ratios, though. The emulator
doesn't charge for writing to the card; on a real Agon that 1 MiB takes 15.2s.

I'm glad the gap closed. Two independent assemblers that agree byte for byte
are more useful than one, because each keeps the other honest.

## Libraries for C

zap can also assemble relocatable objects, so you can write the fast parts of
a C program in assembly and link them in. `-f elf` writes an ELF object for
[agondev](https://github.com/AgonPlatform/agondev) on a PC, and `-f acc`
writes one for acc, the C compiler that runs on the Agon itself:

    zap bytes.s bytes.o -f elf      # for agondev
    zap bytes.s bytes.o -f acc      # for acc

The same source works for both. `XDEF` exports a routine, `XREF` imports a C
function or variable, and `SEGMENT CODE`, `DATA`, `RODATA` and `BSS` say where
things go. [Using zap with agondev](docs/zap-with-agondev.md) and
[using zap with acc](docs/zap-with-acc.md) walk through a complete example
with each, and [docs/LIBRARIES.md](docs/LIBRARIES.md) has the details.

## Getting it

Grab `zap.bin` from the [latest release](https://github.com/avalonbits/zap/releases/latest).
It's already built for the Agon, so there's nothing to compile.

1. Download [`zap.bin`](https://github.com/avalonbits/zap/releases/latest/download/zap.bin).
2. Copy it into `/bin` on the Agon's SD card.
3. Run `zap hello.s hello.bin`.

`zap -v` prints the version. The current release is
[v1.1.0](https://github.com/avalonbits/zap/releases/tag/v1.1.0).

## Building it yourself

For the Agon, with the [agondev](https://github.com/AgonPlatform/agondev)
toolchain on your `PATH`:

    make                    # produces bin/zap.bin

For the host, to try it out or run the tests:

    cc -std=gnu11 -O2 -fsigned-char -include test/stubs/host_types.h \
       -Isrc -Itest/stubs -o zap src/*.c test/stubs/agon_stubs.c

## Usage

    zap <source> [<output>] [options]

    zap game.s game.bin                 # assemble
    zap game.s                          # output named game.bin
    zap game.s game.bin -l -s           # with a listing and a symbol file
    zap game.s game.bin -o 40000 -b 00  # origin 0x40000, fill byte 0x00
    zap game.s game.bin -w              # warn about truncated values

Options can go before or after the file names, and `-o 50000` and `-o50000`
mean the same thing.

| Option | Meaning |
| --- | --- |
| `-v` | Print the version and exit |
| `-h` | List the options |
| `-o <hex>` | Origin address in hex. Default `040000` |
| `-b <hex>` | Fill byte for reserved space in hex. Default `FF` |
| `-a <0\|1>` | ADL mode. Default `1` |
| `-l` | Write a listing to `<source>.lst` |
| `-d` | Print the listing to the console instead |
| `-s` | Write the global symbols to `<source>.symbols` |
| `-x` | Print assembly statistics at the end |
| `-c` | No colour in messages |
| `-w` | Warn when a value doesn't fit where it's written |
| `-i` | Accepted for compatibility (truncation warnings are already off) |
| `-m` | Accepted for compatibility (zap has only one memory mode) |
| `-ez80` | Use ez80asm's expression rules (see below) |
| `-f elf\|acc` | Write a relocatable object for agondev or acc instead of a flat binary (see [Libraries for C](#libraries-for-c)) |

## What it assembles

**Instructions:** the full eZ80 set, including the `.SIS` `.LIS` `.SIL` `.LIL`
mode suffixes and their short forms (`.S`, `.L`, `.IS` and so on).

**Labels:** global labels, local labels scoped to the previous global
(`@name`), and anonymous labels (`@@`, referred to as `@f` and `@b`).

**Expressions:** `+` `-` `*` `/` `<<` `>>` `&` `|` `^`, unary `-` and `~`,
parentheses, `$` for the current address, character literals with escapes,
and numbers written as `1234`, `0x1234`, `$1234`, `1234h`, `0b1010`, `%1010` or
`1010b`.

**Directives:**

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
| Objects only (`-f`) | `XDEF` (`.GLOBAL`), `XREF` (`.EXTERN`), `SEGMENT` (`.SECTION`, `.TEXT`, `.DATA`, `.RODATA`, `.BSS`) |

Directives are case-insensitive and the leading `.` is optional. `.CPU` picks
an instruction set: the Z80 set includes the undocumented instructions, and
neither Z80 nor Z180 allows ADL mode or mode suffixes.

## Compatibility with ez80asm

zap is tested against ez80asm 2.3, the version vendored under `test/ref`. All
507 sources in ez80asm's own test suite either produce identical bytes or are
rejected by both, and the same goes for BBC BASIC for Agon, Rokky, and zap's
own 56 regression sources.

Version 2.3 changed a few things compared to 2.2, and zap follows 2.3:

- A forward-referenced bit number that's out of range (`bit n, a` with
  `n: EQU -1`) is now an error. Written directly, `bit -1, a` still assembles
  to `cb ff`, because ez80asm checks the two cases differently.
- Index displacements are checked as 24-bit values, so `(ix+0x40018)` is out
  of range instead of wrapping to offset `0x18`.
- `FILLBYTE` only affects space reserved after it. In 2.2, space reserved
  before the first `FILLBYTE` picked up the file's last one.
- `DS` evaluates its (ignored) initializer: `ds 4, 0xFF` no longer warns, and
  `ds 4, nope` with `nope` undefined is an error.
- Listings have a fixed-width depth column after the line number.

### The `-ez80` flag

By default zap evaluates expressions the way a C programmer would expect.
`-ez80` reproduces three quirks of ez80asm instead, and the tests and
benchmarks use it:

- No operator precedence: `1+2*3` is 9, evaluated left to right.
- `IF a == b` evaluates `a` and ignores the rest, so `IF 0 == 0` is false.
- `0bh` is hex, not binary: the `h` suffix wins over the `0b` prefix.

Everything else behaves the same in both modes.

## Diagnostics

Errors give the file, line and message, echo the line, and for errors inside
a macro, say where it was invoked:

    Macro [mos_call] in "kernel.s" line 12 - unknown label 'MOS_SYSVARS'
      ld a, MOS_SYSVARS
    Invoked from "main.s" line 84 as
      mos_call MOS_SYSVARS

With `-w`, values that don't fit their field are reported and assembly
continues:

    File "game.s" line 31 - Value truncated to 8 bit '0x100'

A value fits if its bytes mean the same number whether read as signed or
unsigned, so `ld a, -1` and `ld a, 255` are fine but `ld a, 256` isn't. The
check is off by default because it costs about 2% of the run time.

## Testing

    test/run.sh                    unit and CLI tests, plus test/cases compared
                                   byte for byte against ez80asm
    test/corpus.sh                 ez80asm's test suite plus zap's regression
                                   sources, compared the same way
    test/corpus.sh --regress       just zap's own sources (a couple of seconds)
    ZAP_WINDOW=512 test/corpus.sh  the same with a tiny output window, so
                                   everything goes through the streaming path
    FIX_CAP=1024 test/corpus.sh    the same with a capped fixup list
    test/window.sh                 a large generated source with every kind of
                                   fixup patched long after it was written
    test/object.sh                 objects: linked by agondev's ld and compared
                                   with flat output, and ACC objects compared
                                   with acc's own writer
    test/abi.sh                    C built by agondev and by acc calling zap's
                                   objects, run on the emulator
    test/bench/bench.sh            timings against ez80asm on the emulator

The reference assembler and its test suite are vendored, so none of this needs
network access. The benchmarks and `test/abi.sh` need
[fab-agon-emulator](https://github.com/tomm/fab-agon-emulator), and the object
tests need agondev (and acc's source tree for the ACC checks); they're
skipped without them. Everything else runs on the host.

## Documentation

- [`docs/DESIGN.md`](docs/DESIGN.md) explains how the assembler works.
- [`docs/zap-with-agondev.md`](docs/zap-with-agondev.md) and
  [`docs/zap-with-acc.md`](docs/zap-with-acc.md) show how to call assembly
  from C with each compiler, and [`docs/LIBRARIES.md`](docs/LIBRARIES.md)
  covers how objects work.
- [`ez80_advanced_optimization_guide.md`](ez80_advanced_optimization_guide.md)
  covers writing fast C for the eZ80: which ordinary C turns into library
  calls, what inlining really does, a couple of compiler bugs, and how to
  measure. It's most of the reason zap is fast.

## License

GPL-3.0. The vendored ez80asm binaries and test suite under `test/` are MIT
licensed and belong to the
[agon-ez80asm](https://github.com/AgonPlatform/agon-ez80asm) project.
