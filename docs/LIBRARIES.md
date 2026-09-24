# Assembly libraries for C

This is the design for letting zap produce relocatable objects, so functions
written in assembly can be linked into C programs built with agondev on a PC
or with acc on the Agon itself. It's being built in the steps listed under
[Plan](#plan). Steps 1 to 4 are in: `-f elf` writes an object with segments,
exports and imports, and every relocation type below, and C built by agondev
calls into it and back out on the emulator.

## Why objects

zap currently produces a flat binary: every address is fixed when the file is
assembled. A library can't work that way, because its code ends up wherever
the linker puts it and calls functions whose addresses it doesn't know. An
object file leaves those addresses as relocations for the linker to fill in.

agondev and acc share a calling convention, so one assembly source can serve
both. Only the object format differs:

| | agondev | acc |
|---|---|---|
| format | ELF32 relocatable (`EM_Z80`, flags `0x84`) | ACC v6 (acc's `src/obj.c`) |
| linker | `ez80-none-elf-ld` | acc |
| archives | `ez80-none-elf-ar` | `acc -a` |

The ELF writer comes first, because binutils can check its output
independently. The ACC writer follows. acc's `docs/object-format-vs-elf.md`
explains why acc keeps its own format rather than using ELF.

## Command line

    zap lib.s lib.o -f elf     an ELF object for agondev
    zap lib.s lib.o -f acc     an ACC v6 object for acc
    zap lib.s lib.bin          a flat binary, as today

With `-f`, the output name defaults to `<source>.o`. Without it, nothing
changes. `-o` and `-a 0` mean nothing in an object and are refused with `-f`,
and so are `-l`, `-d` and `-s` until they're written for objects.

## Writing a library

```asm
        XREF    _malloc                 ; defined elsewhere
        XDEF    _sum3                   ; callable from C as sum3()

        SEGMENT CODE
_sum3:  ld      iy, 0
        add     iy, sp
        ld      hl, (iy+3)              ; first argument
        ld      de, (iy+6)
        add     hl, de
        ld      de, (iy+9)
        add     hl, de
        ret                             ; result in HL

        SEGMENT DATA
table:  dl      _sum3, 7

        SEGMENT BSS
count:  ds      3
```

### Directives

These exist only when `-f` is given. In a flat assembly they're ordinary
words, as they are to ez80asm, so an existing source that uses one as a macro
name keeps working.

| directive | GNU spellings accepted | meaning |
|---|---|---|
| `XDEF name[, name...]` | `.global`, `.globl` | export a label |
| `XREF name[, name...]` | `.extern` | import a label defined in another object |
| `SEGMENT CODE` | `.section .text`, `.text` | following code and data go in `.text` |
| `SEGMENT RODATA` | `.section .rodata`, `.rodata` | read-only data |
| `SEGMENT DATA` | `.section .data`, `.data` | initialised data |
| `SEGMENT BSS` | `.section .bss`, `.bss` | zero-filled space, reserved with `DS` |

The spellings are ZDS II's, because that's what existing Agon assembly uses:
of 86 Agon projects on GitHub, 31 use `XDEF`, 27 `XREF` and 33 `SEGMENT`,
against 3 using GNU's `.global` or `.extern`. ZDS's `XREF name:ROM` form is
accepted and the suffix ignored. ZDS's `DEFINE` and its other segment names
(`STRSECT`, `LORAM`, `.STARTUP`) are refused for now.

Labels before the first `SEGMENT` are in `CODE`. An `XDEF` label must be
defined somewhere in the file, and an `XREF` label must not be. Referring to a
label that is neither defined nor imported is an error, as it is today.

### Names

A symbol's name is written to the object exactly as it is in the source. C
names start with an underscore in both toolchains: C's `sum3` is `_sum3` to
agondev and to acc. A label without a leading underscore can still be exported
and imported between assembly objects, but C can't name it.

### Calling convention

Both compilers follow agondev's convention, which acc's `test/abi.sh` pins
and zap's `test/abi.sh` runs against zap's objects:

| | |
|---|---|
| arguments | pushed right to left, each in a 3-byte slot (6 for `long`, 9 for `long long`); the first is at `(sp+3)` on entry |
| cleanup | the caller removes the arguments |
| results | `A` for 1 byte, `HL` for 2 or 3, `E:HL` for 4, `HL`, `DE`, `BC` for 8 |
| struct results | through a hidden first argument; the pointer comes back in `HL` |
| registers | a function may change every register except `IX` and `SP` |

### Using it with agondev

Assemble the library with zap, put it in an archive in the project's `lib`
directory, and name it in the project's `Makefile`; agondev links `LIBS`
before its own library:

    zap mylib.s mylib.o -f elf
    ez80-none-elf-ar rcs lib/libmylib.a mylib.o

    # Makefile
    NAME=myprog
    LIBS=-lmylib
    include $(shell agondev-config --makefile)

C declares the functions as usual, without the underscore:
`extern int sum3(int a, int b, int c);` for `_sum3`. `test/abi/` is a
complete example, and `test/abi.sh` builds it and runs it on the emulator.

## Values and relocations

In an object, a label's address isn't known until the program is linked. So a
value is either absolute (a number, or an `EQU` of one) or relocatable: a
label, or `$`, plus a constant.

| expression | result |
|---|---|
| absolute op absolute | absolute, as today |
| relocatable ± absolute | relocatable |
| relocatable − relocatable, same segment, both defined here | absolute (sizes and distances), usable wherever a number is: `len: equ $ - msg`, `IF`, `DS`, `(end - start) / 2` |
| `rel & 0xFF`, `rel >> 8`, `rel >> 16` in a byte-wide field | relocatable byte |
| anything else involving a relocatable value | an error |

Where a relocatable value is written, zap writes a relocation instead of an
address:

| written as | ELF | ACC v6 |
|---|---|---|
| a 24-bit field: `call`, `ld hl, label`, `dl label` | `R_Z80_24` | `ABS24` |
| a 16-bit field: `dw label`, `ld.sis hl, label` | `R_Z80_WORD0` | `ABS16` |
| `label & 0xFF`, or a label in a byte field | `R_Z80_BYTE0` | `LOW8` |
| `label >> 8` in a byte field | `R_Z80_BYTE1` | `HIGH8` |
| `label >> 16` in a byte field | `R_Z80_BYTE2` | `UPPER8` |
| `jr` / `djnz` to a label in another object or segment | `R_Z80_8_PCREL` | `PCREL8` |

A `jr` or `djnz` within the same segment is resolved by zap and needs no
relocation. For a 16-bit or 8-bit field zap writes `WORD0` and `BYTE0`, not
the `R_Z80_16` and `R_Z80_8` GNU `as` writes for `.dw` and `.db`: `ld`
refuses those when the address doesn't fit, where zap truncates it, as a
flat assembly does. A linked object is always the bytes a flat assembly
would have been.

Where the labels in an expression are still ahead, zap keeps it as text and
works it out once the source has been read, as it does in a flat binary; `$`
and the anonymous labels keep the meaning they had where it was written. An
expression whose labels are all known already is settled on its own line,
which is what lets `ld a, @table >> 8` name a local label.

### What's refused in an object

| | why |
|---|---|
| `ORG`, `.RELOCATE` | an object has no address until it's linked |
| `ASSUME ADL=0`, `.CPU Z80`, `.CPU Z180` | Z80-mode relocations aren't supported yet |
| `ALIGN` above 32768 | ACC records alignment as 4 bits of log2 |
| an instruction or `DB` in `BSS` | the bss holds no bytes |
| `IF`, `ALIGN`, `DS` or `BLK` with a relocatable value | these need a number while assembling |
| `EQU` of a relocatable value | an `EQU` is a number; a distance works |
| a byte of an address in a wider field, or anything more done to it | a relocation takes the byte of the address plus a constant, and nothing else |
| `XDEF` of an absolute `EQU` | allowed for ELF (an absolute symbol); refused for ACC, which has no absolute symbols |
| `bit`, `rst` or `im` with a relocatable operand | these go into the opcode, where no relocation can reach |
| `dw32` of a label | neither format has a 32-bit relocation zap uses |

### Directives that behave differently

- `DS` and `BLK` in `CODE`, `RODATA` or `DATA` always write their bytes. A
  flat binary drops a reservation at the very end of the file; an object
  doesn't, because that space belongs to a variable.
- `DS` in `BSS` reserves space and writes nothing.
- `ALIGN n` aligns relative to the start of the segment and raises the
  segment's alignment, so the linker places the whole segment on an
  `n`-byte boundary.
- `INCBIN` and `FILLBYTE` work as they do today, in the current segment.

## The output formats

**ELF.** An ELF32 relocatable file with `.text`, `.rodata`, `.data` and `.bss`
sections, a `.rela` section for each one that has relocations, and a symbol
table: section symbols, then exported labels and the imported ones a
relocation uses, sorted by name. Relocations are `RELA`, with the addend in
the entry and zeros in the field, as GNU `as` writes them. A label defined in
the object, exported or not, is relocated against its section symbol plus its
offset, as `as` does; only an import is relocated against its own symbol.
Labels that aren't exported aren't written.

**ACC v6.** `CODE`, `RODATA` and `DATA` go into acc's single text blob in that
order, each padded to its own alignment, and `BSS` becomes the bss length and
alignment. The file is one item, whose alignment is the largest `ALIGN` in the
text. Symbols are sorted by name, and only the undefined ones a relocation
uses are written. Relocations whose addend fits the field go in the main
table; `HIGH8` and `UPPER8` always go in the second table with a 24-bit
addend. The build id and dependency count are 0. acc's `test/objv6.py` writes
the same format independently.

## How it's tested

- **Linking at two addresses.** An object linked at address A must produce
  exactly the bytes zap produces for the same source as a flat binary with
  `ORG A`, and the same again at a second address B. Flat output is already
  checked byte for byte against ez80asm, so this ties objects to that check,
  and using two addresses catches a missing relocation that one address could
  hide.
- **The corpus.** Every corpus source that already matches ez80asm and uses
  none of the refused directives goes through the two-address test.
- **Calling from C.** Assembly functions taking every argument width and
  returning every result type, called from C built with agondev (and with acc
  once the ACC writer exists), run on the emulator. Assembly also calls back
  into C through `XREF`.
- **The formats.** Every ELF object zap writes must be accepted silently by
  `readelf` and `ld`. Every ACC object must match what `objv6.py` writes for
  the same content, byte for byte.
- **Errors.** Every refusal above has a test.

## Plan

1. Object mode: `-f`, segments held in memory, the new directives and
   refusals, and an ELF writer for code with no relocations yet. **Done.**
2. Relocatable labels: `XDEF`, `XREF`, 24-bit relocations, the symbol table,
   the two-address test and the corpus. **Done.**
3. The other relocation types, label differences, and `$`. **Done.**
4. Calling-convention tests with agondev on the emulator. **Done.**
5. The ACC v6 writer, tested against `objv6.py` and with acc on the emulator.

Each step is its own pull request, and each is checked against the bbcbasic
and rokky benchmarks so that flat assembly doesn't slow down.
