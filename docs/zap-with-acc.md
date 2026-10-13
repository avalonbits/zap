# Using zap with acc

[acc](https://github.com/avalonbits/acc) is a C compiler that runs on the
Agon itself. zap can assemble a library for it: write the time-critical parts
in assembly, assemble them into an ACC object, and link that into your acc
program. Both tools run on the Agon, so the whole build can happen on the
machine. acc also has a host build, if you'd rather work on a PC.

This walks through the example in [`examples/`](examples/): a small library
of byte routines in [`bytes.s`](examples/bytes.s) and a C program,
[`src/main.c`](examples/src/main.c), that calls them. The same two files build
with agondev too; see [zap-with-agondev.md](zap-with-agondev.md). Everything
here was checked with acc 0.5.0.

## Installing

- **acc:** unzip `acc-<version>.zip` from
  [acc's releases](https://github.com/avalonbits/acc/releases) onto the root
  of the SD card. It puts `acc.bin` in `/bin`, and acc's headers and libraries
  in `/lib/acc`, where acc finds them by itself.
- **zap:** copy `zap.bin` from
  [zap's releases](https://github.com/avalonbits/zap/releases/latest) into
  `/bin`.

Both then work as commands from any directory.

## Build the example

Copy `bytes.s` and the `src` directory to the card, and in that directory:

    */ zap bytes.s bytes.o -f acc
    */ acc src/main.c bytes.o -o bytes.bin
    */ bytes
    Hello from zap
    fill and sum_bytes: 70
    apply_twice(triple, 5): 45
    apply_twice was called 2 times
    all correct

The first line assembles the library as an ACC object. The second compiles
the program and links it with the library in one go; acc adds its own C
library and runtime without being asked, so nothing else goes on the line.

The same can be done a step at a time, which is the way to go once a program
has several C files. `acc -c` writes `src/main.o` beside its source, and does
nothing the next time if the source hasn't changed:

    */ zap bytes.s bytes.o -f acc
    */ acc -c src/main.c
    */ acc src/main.o bytes.o -o bytes.bin

Those three lines make a good obey file: put them in `build.obey` and run
`obey build.obey` after each edit.

On a PC, acc's host build takes the same commands and finds its headers and
libraries in its own tree.

## A library of your own

A library you'll use from several programs is better kept as an acc archive.
A link then takes only the parts a program calls:

    */ zap bytes.s bytes.o -f acc
    */ acc -a bytes.a bytes.o
    */ acc src/main.c bytes.a -o bytes.bin

Several objects, from zap or from acc, can go in one archive.

## Writing the assembly side

The rules are the same as for agondev, because acc uses agondev's calling
convention:

- **Names.** A C function `fill` is the symbol `_fill`. Give your routine
  that name and export it with `XDEF _fill`. A label without the underscore
  is still fine inside the library, but C can't call it.
- **Arguments** are on the stack, pushed right to left, three bytes each
  (six for a `long`, nine for a `long long`). The first is at `(sp+3)` when
  your routine starts. The usual way in is `ld iy, 0` / `add iy, sp`, then
  `(iy+3)`, `(iy+6)` and so on. Don't pop them: the caller does.
- **Results** come back in A for a `char`, HL for a `short`, `int` or pointer,
  E:HL for a `long`, and HL, DE, BC for a `long long`. A struct is returned
  through a pointer the caller passes as a hidden first argument; put the
  pointer back in HL.
- **Registers.** You may change any of them except IX and SP. C uses IX for
  its stack frame.
- **Segments.** `SEGMENT CODE`, `SEGMENT RODATA`, `SEGMENT DATA` and
  `SEGMENT BSS` (or `.text`, `.rodata`, `.data`, `.bss`). acc keeps the first
  three together in one block, in that order, and the bss after the program.
  In the bss, `DS` reserves zeroed space and nothing can be written.
- **Calling C.** Declare the function with `XREF _name`, push its arguments
  right to left, `call _name`, and pop them afterwards. C globals work the
  same way: `XREF _counter`, then `ld hl, (_counter)`.

On the C side, declare what the library offers as `extern`, without the
underscore:

    extern void fill(void *dst, unsigned char value, int len);
    extern const char greeting[];

## When zap says no

Everything [zap-with-agondev.md](zap-with-agondev.md#when-zap-says-no) lists
applies here too, plus one more: an ACC object can't export a number, so
`XDEF` of an `EQU` is refused. Export a label instead, or put the value in a
header for C.

The full list of what's allowed in an object is in
[LIBRARIES.md](LIBRARIES.md), which also describes the ACC format zap writes.

## Checking it

`test/abi.sh` builds this example with acc exactly as above and runs it on
the emulator, and `test/object.sh` checks every ACC object zap writes against
acc's own independent writer, byte for byte.
