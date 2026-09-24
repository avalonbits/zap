# Using zap with acc

acc is a C compiler that runs on the Agon itself. zap can assemble a library
for it: write the time-critical parts in assembly, assemble them into an ACC
object, and link that into your acc program. Both tools run on the Agon, so
the whole build can happen on the machine, as well as on a PC with acc's host
build.

This walks through the example in [`examples/`](examples/): a small library
of byte routines in [`bytes.s`](examples/bytes.s) and a C program,
[`src/main.c`](examples/src/main.c), that calls them. The same two files build
with agondev too; see [zap-with-agondev.md](zap-with-agondev.md).

## Build the example

From `docs/examples`:

    zap bytes.s bytes.o -f acc              # the library, as an ACC object
    acc -c src/main.c -o main.o             # the program
    acc main.o bytes.o libc.a -o bytes.bin  # linked with acc's C library

`libc.a` is the C library that comes with acc. On a PC it's `bin/libc.a` in
acc's tree, and `-I` points `acc -c` at acc's `include` directory. On the
Agon, use wherever your acc installation keeps them.

Copy `bytes.bin` to the Agon's `/bin` and run `bytes`:

    Hello from zap
    fill and sum_bytes: 70
    apply_twice(triple, 5): 45
    apply_twice was called 2 times
    all correct

## A library of your own

A library you'll use from several programs is better kept as an acc archive.
A link then takes only the parts a program calls:

    zap bytes.s bytes.o -f acc
    acc -a bytes.a bytes.o
    acc main.o bytes.a libc.a -o bytes.bin

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
