# How zap works

zap assembles eZ80 source into a binary in **one pass**, in a **single
translation unit**, on a machine with **512 KB of RAM and no cache**. Those
three facts explain most of the design; this document describes the parts and
how they fit together.

The code is `src/zap.c`, about 9,000 lines, divided into sections whose banners
match the headings below. The instruction table is generated into
`src/isa_table.c`; the buffered reader, the number parser and the character
conversions are small units of their own.

---

## 1. The shape of a run

```
main
 ├── parse_args            the command line, in the reference's forms
 ├── run                   ── open the source, size the output buffer
 │    ├── run_lines        ── the line loop: one line at a time, to the end
 │    ├── scope_end        ── settle the last local scope
 │    └── resolve_fixups   ── deferred expressions, block fills, then every
 │                            forward reference patched in the output buffer
 ├── write the output file
 └── listing, symbol file, statistics  (each optional, none can fail the run)
```

There is no intermediate representation and no syntax tree. A line is read,
turned into bytes, and forgotten. What survives a line is only what a later
line might need: the symbols it defined, the fixups it left behind, and the
bytes themselves.

---

## 2. One pass, and what it costs

A reference to a label further down the file cannot be resolved where it is
read. A two-pass assembler reads the source twice; zap reads it once and
**patches the output afterwards**:

1. The instruction is emitted with zeroes where the address goes.
2. A **fixup** records the symbol, the offset in the output, the width, and the
   line, so a failure can be reported against the line that used the label.
3. When the label's value becomes known -- at the end of the scope for a local,
   at the end of the source for a global -- the recorded bytes are overwritten.

The price is that the whole output has to be in memory at once. On a 512 KB
machine that bounds what can be assembled, and it is the trade the design
makes: memory for a second pass over the source.

Three kinds of thing wait for the end of the run:

| | resolved by | holds |
|---|---|---|
| fixup | `patch_fixup` | one or two symbols, an addend, a width, an offset |
| deferred expression | `resolve_deferred` | expression text a fixup cannot represent |
| deferred fill | `resolve_fills` | a `BLK` whose fill value was not known yet |

A fixup's width is normally a byte count, 1 to 4, or 0 for a relative
displacement. Three values above those mean the operand belongs in the
**opcode byte itself** rather than after it -- a bit number, an interrupt mode,
a restart address -- so `bit n, a` with `n` defined later still assembles
correctly.

---

## 3. The state

Everything the assembler knows lives in one file-scope object, `state`, of type
`zap_state`. A file-scope object is addressed absolutely on the eZ80: the
address of a field is a constant written into the instruction. Reached through
a pointer parameter instead, every access would first load that pointer out of
the frame.

It holds the output buffer and cursor, the origin, the ADL mode, the symbol
table, the local scope, the fixup lists, the macro list, the reader, the
diagnostic state, and the listing state. The consequence is that **one process
assembles one source**; a library API that could assemble two would have to
pass this around again.

Field order inside the struct is deliberate. The fields a line touches --
the cursor, the line number, the error code -- are near the front, and the
bulky, rarely-read ones (the 256-byte local bucket array, the include path) are
at the end, so that a displacement from a base register reaches the hot ones.

---

## 4. Reading the source

`buf_reader` hands out **whole lines**. It reads a buffer, trims the read back
to the last newline in it, and carries the partial line at the end forward to
the front of the next buffer. Two consequences the rest of the assembler relies
on:

* a token can point straight into the buffer, because a refill only ever
  happens at a line boundary;
* there is always a newline one byte past the content -- a **sentinel** -- so
  every scan terminates on it without testing the end.

`INCLUDE` opens a second reader and re-enters the same line loop; the parent's
reader is saved in the include's own stack frame. The parent's file handle is
closed while the child runs and reopened afterwards, seeking back to where the
parent had reached, because MOS has few handles.

---

## 5. Assembling a line

`assemble_line` is the hot path, and everything it needs is inlined into it:
the label parser, the mnemonic lookup, the operand parser, the row matcher and
the emitter. A line is examined once, left to right.

```
label?   ──►  define it, or set it aside for EQU
mnemonic ──►  found?   ──► parse operands ──► match a row ──► emit bytes
             not found ──► directive?  macro?  suffixed instruction?  error
```

* **Labels.** A name at the start of a line, with or without a colon. A
  trailing `EQU` names a value instead of an address.
* **The mnemonic lookup** buckets by first letter *and* length, so a lookup
  compares one or two candidates rather than five, and never measures a length
  at run time.
* **Operands** are parsed into a fixed 21-byte struct: a register-set bitmask
  in three byte-wide planes, a mode (register, indirect, immediate,
  indirect-immediate), an immediate, a displacement, and any forward reference.
* Anything that is not an instruction -- a directive, a macro invocation, an
  instruction with a `.LIL`-style suffix, a three-operand `RES`/`SET` -- is
  reached by a **tail call** from the point where the mnemonic lookup failed,
  so an ordinary instruction never tests for any of them.

---

## 6. Selecting an instruction

`src/isa_table.c` holds the whole instruction set, transcribed mechanically
from the reference assembler and generated by `tools/gen_isa.py`. It is
arranged in three levels:

```
mnemonic  ──►  mode groups  ──►  rows
  "ld"          (reg, imm)       one per encoding
```

* A **row** says what the two operands must be (register sets and conditions),
  how each folds into the opcode, the prefix and opcode bytes, which CPUs have
  it, and which mode suffixes it accepts.
* A **mode group** collects the rows of one mnemonic that expect the same
  operand shapes, so a group whose shape does not match is rejected once rather
  than row by row. The four mnemonics that take condition codes -- `call`,
  `jp`, `jr`, `ret` -- are left ungrouped, because a condition code can arrive
  in a shape the group test would reject.
* `match_row` tests operand A first and reaches B only if A survives. Most
  rejections are rows of the right shape with the wrong registers.

Once a row is chosen, `emit_row` writes the bytes: the mode-suffix byte if
there is one, the index prefix, the opcode prefix, the opcode with its operands
folded in, the index displacement, then the immediates. `bit n, (ix+d)` is the
one shape where the displacement comes *before* the opcode.

**`.CPU`** selects an instruction set by masking rows: the Z80 set includes the
undocumented instructions, and neither the Z80 nor the Z180 has ADL or mode
suffixes.

---

## 7. Symbols

Three kinds, in two tables.

**Global labels and EQU values** live in a 2,048-bucket table keyed by a
Pearson hash of the name plus its first character, last character and length.
A name is **interned on first sight**, defined or not, so a reference to a
label that has not appeared yet gets an entry and a fixup points at it. Nodes
and names come from arenas of blocks that never move: a growing array would
have to be reallocated, and a realloc that moves holds both copies at once.

**Local labels** (`@name`) belong to the global label above them. Their table
is 64 buckets, and a scope ends at the next global label -- thousands of times
in a real source -- so it must empty in constant time. Each slot carries the
generation it belongs to: advancing the generation counter makes every bucket
read as empty, whatever chain it still holds.

**Anonymous labels** (`@@`, referred to as `@f` and `@b`) are not table
entries at all: the assembler keeps the address of the last `@@` for `@b`, and
one nameless symbol that every `@f` since the last `@@` waits on, settled the
moment the next `@@` appears.

When a scope ends, its local fixups are patched, and any fixup that names a
global *and* a local has the local half folded into its addend there and then
-- the local's node is about to be recycled.

---

## 8. Expressions

Most operands never reach the evaluator: a register, a plain literal and a bare
name each have a reader of their own. What does reach it is a precedence climb
over `+ - * / << >> & | ^` with unary `-` and `~`, grouped with `[...]`
because parentheses already mean indirection.

Two things make it unusual:

* **Two binding-power tables.** Under `-ez80` every operator binds equally,
  and a precedence climb in which everything binds equally is exactly the
  left-to-right fold the reference performs. The compatible behaviour falls out
  of the same code.
* **The evaluator is 32 bits wide** where the machine's word is 24. `DW32` and
  `BLKL` are four bytes, and the reference evaluates in 32 bits; truncation
  happens at the emitter, on the width the directive asked for. The fast
  readers stay in the machine's word and hand anything wider to the general
  parser, so the extra width is not paid on the common path.

While an expression is evaluated, the labels it names that are still undefined
are tracked with their signs. A fixup can carry two symbols and a constant, so
`end - start` and `k + a - b` become fixups; anything more complicated is kept
as **text** and evaluated again at the end of the run, when everything is
known.

---

## 9. Directives

Reached only after the mnemonic lookup has failed, and dispatched by a switch
on the token's length and characters rather than a table.

| group | directives |
|---|---|
| data | `DB` `DW` `DL` `DW24` `DW32` `ASCIZ` (and the `DEFB`/`BYTE`/`ASCII` spellings) |
| space | `DS` `BLKB` `BLKW` `BLKP` `BLKL` `ALIGN` `FILLBYTE` |
| address | `ORG` `.RELOCATE` `.ENDRELOCATE` `ASSUME ADL=` |
| symbols | `EQU` |
| files | `INCLUDE` `INCBIN` |
| conditional | `IF` `ELSE` `ENDIF` |
| macros | `MACRO` `ENDMACRO` |
| target | `.CPU` |

Two distinctions in this group are easy to get wrong and are worth stating:

* **`DS` reserves, `BLK` emits.** Reserved space that reaches the end of the
  file with nothing after it is not written at all; a block always is.
  `FILLBYTE` sets what a reservation is filled with.
* **`ORG` is two directives sharing a name.** The first in a file moves the
  origin; every later one pads out to its address.

The conditional directives are ordered in the enum so that a line inside a
switched-off branch can decide what to do with a single comparison: everything
at or above `IF` is still handled while skipping, everything below it is
skipped.

---

## 10. Macros

A definition captures the body **as text**, and finds the places its parameters
occur *once*, when the body is read. Each occurrence is recorded as an offset,
a parameter number and a length.

An invocation:

1. reads its arguments as spans of the invocation line;
2. enters a local scope of its own, if the body has any local labels in it;
3. for each body line, copies text up to the next mark, copies the argument,
   and carries on -- then hands the line straight to `assemble_line`.

There is no reader and no nested line loop for an expansion: the body is
already a run of lines. Substitution is textual and by whole identifier, which
is what the reference does -- with `x` bound to `1+1`, `db 10-x` is ten there,
not eight.

Nesting is bounded at eight levels -- the same bound as INCLUDE -- and each
level has its own substitution buffer, kept and grown between invocations
rather than allocated per expansion.

---

## 11. Diagnostics

Errors are **codes**, not strings: `state.err` is a `zap_err`, and the message
text lives in one table beside the enum. A caller other than `main` can branch
on the code, which is what makes the assembler usable as a library.

Everything a report needs is **captured at the moment of failure and never
maintained in advance**, so a source that assembles cleanly pays nothing:

* the failing line, copied out of the reader's buffer;
* the token the message is about, where the site that failed had it in hand;
* for a failure inside a macro, the body line *and* the line that invoked it.

```
Macro [mos_call] in "kernel.s" line 12 - unknown label 'MOS_SYSVARS'
  ld a, MOS_SYSVARS
Invoked from "main.s" line 84 as
  mos_call MOS_SYSVARS
```

There is one warning, for a value too large for the space it is written into.
It is the only diagnostic that asks a question of every value in every source
rather than doing work after something has gone wrong, so it is behind `-w`.

---

## 12. Listing and sidecars

`-l` and `-d` write a listing in the reference's columns: address, up to four
bytes per row, line number, and the source line as written. A macro expansion
is listed as the reference lists it -- the invocation with no bytes, the
arguments, then a row per body line tagged with its depth.

A line holding a forward reference is listed before the reference is patched,
so those lines are remembered and their byte columns **written again from the
finished output** before the file is closed. The console listing cannot be
given that treatment and shows the bytes as they were emitted.

`-s` writes the global symbols sorted, in the reference's format. `-x` prints
what the run used. None of the three can fail an assembly: the output file is
already written when they run.

---

## 13. Compatibility

Byte-for-byte agreement with `ez80asm` is the point of the project, so where
the reference does something surprising, `-ez80` reproduces it rather than
being right and incompatible: no operator precedence, `IF a == b` discarding
the comparison, `0bh` read as hex.

Four differences are deliberate and permanent, each argued in
`.internal/completeness.md`: a negative reservation (which the reference turns
into gigabytes of output), a `FILLBYTE` that would retroactively change a
reservation already written, `@local - global` with both still ahead, and
substituting a macro parameter inside a longer identifier.

`-w` is zap's own flag, and the truncation check being off by default is the
one place the two command lines mean different things.

---

## 14. What the target imposes

The eZ80 shapes this code more than any other single factor. Four rules run
through the whole file:

1. **Ordinary C becomes library calls.** A 24-bit AND, a multiply, a variable
   shift, a signed comparison -- each is a call, not an instruction. Byte
   quantities, powers of two and unsigned compares avoid them.
2. **A stack frame must stay under 128 bytes.** A frame displacement is a
   signed byte; past that, every access needs a computed address. Adding three
   bytes to `assemble_line`'s frame is measurable in the whole program.
3. **A `static inline` helper is inlined at the compiler's discretion**, and
   one cold caller can take that away from every hot one. The helpers on the
   hot path carry `always_inline` for that reason.
4. **Every character scan carries its bound.** Without it the compiler may
   rotate the loop so that the first character is never examined -- correct on
   the host, wrong on the target. `test/run.sh` checks the source for it.

`ez80_advanced_optimization_guide.md` is the long form, with the measurements
behind each rule.

---

## 15. How it is checked

| | |
|---|---|
| `test/run.sh` | unit and CLI tests, and every source in `test/cases` assembled by both zap and the vendored reference and compared byte for byte |
| `test/corpus.sh` | the reference's own 507-source corpus plus zap's regression sources, the same way |
| `test/bench/bench.sh` | throughput against ez80asm on the emulator |
| `test/bench/corpus-target.sh` | per-source speedups over the whole corpus, on the Agon |

The rule the project runs on is that **the reference is the oracle**: a
question about what zap should do is answered by assembling the case with
`ez80asm` and reading the bytes, not by reasoning about what an assembler ought
to do.

---

## 16. Adding something

* **An instruction form** belongs in the generator, `tools/gen_isa.py`, not in
  the generated table.
* **A directive** needs a `DIR_` constant, a spelling in `directive_of`, a case
  in `directive_line`, a case file under `test/cases` compared against the
  reference, and a note in the README's directive table.
* **A diagnostic** needs a `zap_err` code and one line in the message table.
  The static assert on the table size will catch a code without text.
* Anything that touches the hot path should be measured on the Agon before and
  after. The host does not predict the target: the same change can read 0.71x
  on a desktop and 0.98x on the machine this is for.
