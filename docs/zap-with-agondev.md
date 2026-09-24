# Using zap with agondev

[agondev](https://github.com/AgonPlatform/agondev) compiles C for the Agon on
a PC. zap can assemble a library for it: write the time-critical parts in
assembly, assemble them into an ELF object, and link that into your agondev
program like any other library.

This walks through the example in [`examples/`](examples/): a small library
of byte routines in [`bytes.s`](examples/bytes.s) and a C program,
[`src/main.c`](examples/src/main.c), that calls them. The same two files build
with acc too; see [zap-with-acc.md](zap-with-acc.md).

## What you need

- zap on your PC (see the README for building it for the host)
- agondev, with its `bin` directory on your `PATH`
- optionally, [fab-agon-emulator](https://github.com/tomm/fab-agon-emulator) to
  run the result without copying it to an Agon

## Build the example

From `docs/examples`:

    zap bytes.s bytes.o -f elf              # the library, as an ELF object
    mkdir -p lib
    ez80-none-elf-ar rcs lib/libbytes.a bytes.o
    make                                    # builds bin/bytes.bin

`make` uses agondev's own makefile. The only thing
[the example's `Makefile`](examples/Makefile) adds is `LIBS=-lbytes`, which
links `lib/libbytes.a` before agondev's own library:

    NAME=bytes
    LIBS=-lbytes
    include $(shell agondev-config --makefile)

Copy `bin/bytes.bin` to the Agon's `/bin` and run `bytes`:

    Hello from zap
    fill and sum_bytes: 70
    apply_twice(triple, 5): 45
    apply_twice was called 2 times
    all correct

You don't need the archive: an object linked straight in works just as well.
The archive is what lets a program take only the parts of a bigger library
it uses, and it's what `LIBS` expects.

## Writing the assembly side

A few rules, all of them visible in `bytes.s`:

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
- **Sections.** `SEGMENT CODE`, `SEGMENT RODATA`, `SEGMENT DATA` and
  `SEGMENT BSS` (or `.text`, `.rodata`, `.data`, `.bss`) put what follows in
  the matching ELF section. In the bss, `DS` reserves zeroed space and nothing
  can be written.
- **Calling C.** Declare the function with `XREF _name`, push its arguments
  right to left, `call _name`, and pop them afterwards. C globals work the
  same way: `XREF _counter`, then `ld hl, (_counter)`.

The full list of what's allowed in an object, and what isn't, is in
[LIBRARIES.md](LIBRARIES.md).

On the C side, declare what the library offers as `extern`, without the
underscore:

    extern void fill(void *dst, unsigned char value, int len);
    extern const char greeting[];

## When zap says no

Some things can't go in an object, and zap tells you when you've used one:

- `ORG` and `.RELOCATE`: an object has no address until it's linked. Leave
  them out and let the linker place it.
- `ASSUME ADL=0` and `.CPU Z80`: object output is ADL (24-bit) code only.
- An address in a place a relocation can't reach, such as a bit number,
  a restart address, an index offset (`(ix+label)`), or a byte of an address
  in a wider field (`ld hl, label >> 8`).
- `EQU` of an address. An `EQU` has to be a number, though a distance such as
  `len: equ $ - msg` is fine.

## Checking it

`test/abi.sh` builds this example (and a larger calling-convention test in
`test/abi/`) exactly as above and runs it on the emulator, so these steps are
known to work.
