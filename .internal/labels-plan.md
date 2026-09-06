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

### Keying the symbol table: measured, not assumed

zap uses a Pearson hash, chosen because it suits an 8-bit machine. Measured
against the labels of the two real programs, **a key built from the first
character, the last character and the length beats it** -- and not for the
reason one would guess.

Label lengths, which decide what a key can use:

| | definitions | distinct | median | p95 | max |
|---|---|---|---|---|---|
| BBC BASIC | 1,642 | 1,620 | 6 | 13 | **20** |
| Rokky | 286 | 246 | 8 | 13 | **20** |

**26 characters survives; 16 does not.** BBC BASIC has 14 labels longer than
16, so a 16-character limit would break real code -- but the *key* can clamp
the length at 8 without limiting the label, which is what was measured. (For
reference, ez80asm allows 64 and zap allows 26, a known incompatibility that
these two programs never reach.)

Probes per reference, over 1,620 distinct labels and 3,618 references:

| scheme | buckets used | max chain | probes |
|---|---|---|---|
| first x length<=8 (208) | 133 | 67 | 12.0 |
| first x length<=16 (416) | 194 | 67 | 11.3 |
| first16 x length<=16 (256, one byte) | 141 | 80 | 15.3 |
| first x second (676) | 199 | 67 | 11.1 |
| Pearson 8-bit (256) | 255 | 14 | 4.2 |
| Pearson 16-bit (8192) | 1109 | 6 | 1.3 |
| **first \| last \| length, powers of two (8192)** | 879 | 19 | 1.7 |

Two things to take from it.

**The first two letters carry almost no entropy.** Assembly labels cluster hard
on prefixes -- `ASC_TO_NUMBER1..4`, `CRTONULL`/`CRTONULL0` -- so first-letter
and first-two-letter schemes leave most buckets empty and run chains of 67 to
80. It is the **last** character plus the length that discriminates.

**Probes are the wrong metric.** Pearson has fewer of them, and still loses,
because hashing is itself a walk over the string. Counting every character
touched end to end -- the hash *and* the comparisons:

| | compare chars | hash chars | total |
|---|---|---|---|
| bbcbasic, Pearson 16-bit (8192) | 5.07 | 5.1 | 10.2 |
| bbcbasic, Pearson 8-bit (256) | 8.02 | 5.1 | 13.1 |
| bbcbasic, **first \| last \| length** | 6.31 | **0** | **6.3** |
| rokky, Pearson 16-bit (8192) | 5.53 | 7.2 | 12.8 |
| rokky, **first \| last \| length** | 5.96 | **0** | **6.0** |

The structural key touches **40 to 55% fewer characters**, and the Pearson
figures are generous: a 16-bit Pearson is two table lookups per character, so
its hash column is really double what is shown.

The compare penalty is real and small. Everything in a structural bucket shares
its first character, last character and length, so a comparison cannot fail
fast -- `ASC_TO_NUMBER1` against `ASC_TO_NUMBER2` is thirteen characters before
it rejects. That costs 1.2 characters per lookup against a hash that costs 5 to
7.

**Building the index without a multiply.** `first << 8 | last << 3 | length` is
constructible byte-wise -- the high byte is the first character masked to 5
bits, the low byte is a 32-entry table lookup for `last << 3` OR-ed with the
clamped length. No shift, so no call. This matters: a constant shift by two is
`call __ishl` on this chip.

**A perfect hash is not available.** Labels are discovered while assembling, so
there is no key set to build one over. Perfect hashing works for a closed set,
which is exactly what dzap already does for mnemonics and registers -- that
part is done and is not what labels need.

### Confirmed on twenty-five programs

The above was two programs, which is a thin sample for a claim about naming
style. `test/corpus-full/harvest.sh` now fetches every Agon project linked from
sabotrax/agon-software -- 86 repositories, 996 assembly files, 181,753 lines,
twelve times the BBC BASIC set. Over the 25 of them with real symbol tables,
14,063 labels and 30,629 resolved references:

| scheme | probes | cmp chars | hash chars | total | worst chain |
|---|---|---|---|---|---|
| Pearson 8-bit (256) | 3.20 | 9.87 | 7.62 | 17.48 | **17** |
| first \| last \| length (2048) | 3.57 | 11.76 | 0 | 11.76 | 45 |
| **first \| last \| length (8192)** | 2.64 | 10.83 | **0** | **10.83** | 36 |

**The result holds and widens**: 10.83 characters against 17.48, a 38%
reduction, on twelve times the sample. Pearson still wins on probes and still
loses overall, for the same reason -- the hash is a walk.

The two caveats also hold, and one is now quantified. The structural key's
worst chain is **36 against Pearson's 17**, so its tail is twice as long even
though its average is better; a program that names everything `loop_1 ...
loop_99` would sit in that tail. And 8,192 buckets is 24 KB at three bytes an
entry, in line with what zap's symbol table already spends but not free.

**And the label length limit is wrong.** The two-program sample said 26
characters was never reached; over 25 programs **39 of 14,063 labels exceed
it**, the longest real one being 38, and they are ordinary names --
`VDU_BufferBitmapExpandMappingBufferBit` and its neighbours in AgonConsole8's
VDU code. ez80asm allows 64. Truncating at 26 would silently merge two labels
that differ only after the limit, which is the worst way to be wrong, so
whatever dzap does must compare the whole name even if it keys on part of it.

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
