# Adding labels to dzap

Written 2026-09-06, against dzap at **318 cycles per source byte** on isa_real
(4.52s for 262,151 bytes, 23,075 lines, 56.6 KiB/s). Everything here is
measured or is marked as a guess.

dzap exists to price features one at a time. Labels are the first real one, and
the point of the exercise is the number they cost -- not getting them in.

## What labels actually weigh

Counted over the two real programs in test/corpus, which is the only honest
sample:

| | instruction lines | carrying a symbolic operand |
|---|---|---|
| BBC BASIC | 9,585 | 3,643 (**38%**) |
| Rokky | 1,296 | 798 (**62%**) |

Plus 11-15% of lines that *define* a label.

**So the symbol path is not an add-on. It is as hot as the register path.**
Roughly half of all lines will do a lookup, and dzap's own source prices zap's
hash at "about a thousand cycles a lookup" against dzap's current 3,611 cycles
per line. A lookup at that price, on half the lines, is +14% before anything
else. That is the number to beat, and it is the number the whole project exists
to produce.

## Where they land in the code as it stands

Three sites, all of which already exist and are already branches:

* **A definition** -- `assemble_line` begins `while (is_space_ch(*p)) p++;`.
  A label is the case where the first character is *not* space. The branch is
  already there, so instruction lines pay nothing for the feature. This is the
  cheap part.
* **A reference** -- `parse_operand`, the alpha path, at the `p = s;` rewind
  (dzap.c:1084). Today that is register-then-literal; it becomes
  register-then-symbol-then-literal. 82% of operands are registers and
  `reg_of_text` fails fast, so the hot case is unaffected.
* **A forward reference** -- the emitter. `emit_imm` writes one or three bytes
  and the TR_REL path writes one, and both already know the cursor.

## The design

**One pass, with back-patching.** dzap's whole advantage is never re-reading a
line, and the output buffer is already held in memory in full, so a forward
reference is a `(offset, width, symbol)` triple recorded and patched at the
end. Two passes would give the line reader back its cost and hide what labels
are worth.

**The symbol table consumes the token scan; it does not re-walk it.** Every
token is walked twice today -- once to find its end, once to compare -- and a
hash would be a third walk. The scan should produce the key as it goes.

Two constraints on that key, both measured this session and both non-obvious:

* **Shifts are calls.** Not just variable shifts: a *constant* shift by two is
  `call __ishl`, and writing it as `x += x; x += x` does not help because LLVM
  canonicalises the adds back into a shift first. A hash built from shifts pays
  a call per character.
* **A byte-wise key does not shift.** Packing up to four characters into a
  union of `int` and `uint8_t[]` is stores, which is what the hex parser
  already does for the same reason -- see `hex_digits`.

So: accumulate the key by byte during the scan that already runs, and let the
mnemonic bucket, `reg_of_text` and the symbol table all take it.

**Where the address comes from.** The emitter already computes
`DZ_ORG + (o - z->out)` for relative jumps. A label's value is the same
expression at the point of definition, so nothing new is needed to know where
we are.

## What not to do

**Do not shrink `dop`. Tried, and it costs.** The reasoning was good and the
measurement disagreed: `dop` is copied twice a line and two of them are 34 of
`assemble_line`'s 100-byte frame, and frame pressure is demonstrably binding --
inlining `assemble_line` into `run` costs **+17.6%** even though the frame
*fits* at 88 bytes, because the allocator spills the loop-carried pointers.

But taking `dop` from 17 bytes to 13, by narrowing `disp` to the one byte the
emitter actually uses, measured **+0.9% on both files**. The narrower field
makes the compiler do byte-width arithmetic where 24-bit was free, and that
costs more than four bytes of LDIR saves. Only the dead `has_disp` field was
worth removing, and that is neutral.

The conclusion for labels: **add the fields they need without trying to pay for
them elsewhere in the struct.** The budget is not in `dop`.

**Do not micro-optimise `match_row` or `emit_row` first.** Both are within a
few percent of their floor, the last two rounds there returned 2% and 0.4%, and
labels do not touch either.

**Do not assume inlining helps.** Seven measured this session: `parse_operand`
−5.1%, `emit_row` −3.8%, `ddfd_prefix` −3.1%, `match_row_cc` −2.2%, but
`hex_digits` **+3.0%**, a `noinline` negate **+1.0%**, and `assemble_line`
**+17.6%**. The rule that fits all seven: inline a *small* body on a *hot*
path, and measure anything else.

## How to price it

The corpus and the benchmarks are now generated from what **ez80asm** accepts,
never from what dzap accepts -- see `dzap/test/cases/gen_opcodes.sh`. That
rule is what makes a feature's cost measurable at all: the previous corpus was
filtered through dzap and hid 53 wrong forms, and the benchmarks built from it
had never once executed the branch that negates a literal.

Labels cannot go into `isa_even`/`isa_real` without changing what those files
measure. Add a third distribution rather than editing them, the way `gen_isa.sh`
was added beside `gen_pure.sh`, and quote the label cost as the difference
between the same source with and without symbolic operands.
