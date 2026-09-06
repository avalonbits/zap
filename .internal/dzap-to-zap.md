# What dzap learns that zap can have

dzap assembles the easy case, so some of what makes it fast is the easy case
paying for itself and some is technique that would work anywhere. Those are
worth telling apart as they happen rather than reconstructing later, because
the first kind is free to port and the second kind has already lost once when
it was tried on zap.

**Every dzap change gets a row here, with its measured number and a verdict.**
Three verdicts:

- **Portable** -- nothing about it depends on there being no labels,
  expressions or fixups. Apply to zap and measure on the real benchmark set.
- **Conditional** -- works in zap only after something else changes, named.
- **Not portable** -- the simplification is doing the work.

## The table

| change | measured on dzap | verdict |
|---|---|---|
| Precomputed mnemonic lengths (was a `strlen` per candidate, 8% of all work) | part of −9.9% | **Portable.** Re-deriving a compile-time constant is a defect anywhere. |
| Character class table for space/name/digit | part of −9.9% | **Portable.** zap's lexer classifies bytes with the same compare chains. |
| Bucket mnemonics by first letter *and* length | part of −9.9% | **Conditional** on Option A in `positional-lexing.md`. dzap can assume a statement start is a mnemonic; zap's lexer is context-free and must find registers, directives and flags through the same lookup. |
| Precomputed row modes and `F_CCOK`, one 16-bit compare per row | part of −3.7% | **Portable.** Pure table preparation; zap runs the identical test in its own `match_row`. |
| Operand cleared by copying a zeroed template | part of −3.7% | **Conditional** on shrinking zap's operand first. dzap's is small; zap's is **153 bytes on the eZ80**, 128 of it the expression buffer for deferred fixups. Copying that would be worse than the ten stores it replaces. |
| Literal fast path: read `0x…` and decimal without `num_parse` | part of −3.7% | **Not portable.** Already tried on zap and reverted: **+3.4% on bbcbasic**, faster only on literal-dense synthetic input. In zap an operand can be a symbol or an expression, so the fast path must fall through to the general evaluator and the fall-through is what costs. dzap's version works because that case does not exist. |
| **Register masks held as `uint24_t` rather than `uint32_t`** | **−12.3%** | **Portable, and the one to take first.** The mask's highest bit is `R_I` at 2^20, so the set fits the eZ80's native word. `src/operand.h:75` declares `uint32_t reg` and `src/isa.h:95` `uint32_t regsetA`; zap runs the same test in the same shape. Nothing to do with labels. |
| Mnemonic's `.` folded into the class table | part of −12.3% | **Portable.** |
| Immediate held as `int` (24-bit) rather than 32-bit `value` | part of −4.7% | **Portable.** An instruction's immediate is at most three bytes; zap's `operand.imm` is the same `value` type and its emitter has the same ceiling. Invisible on the host, like the register masks. |
| No pre-scan for the line end — one pass over the source instead of two | part of −4.7% | **Conditional**, and closer to a redesign than a transplant. zap is driven token-by-token through `lex_next` rather than line-by-line, so the idea (nothing needs the line bound, because no scan can cross a newline) applies but the shape does not. |
| Literal and displacement accumulators narrowed to `int` | part of −2.2% | **Portable.** Third application of the idea behind the two biggest wins; an operand is at most three bytes and a displacement one signed byte. |
| Output reserved once per instruction, not bounds-checked per byte | part of −2.2% | **Portable, and half-built already.** zap has `pr_reserve`; it is `pr_wbyte` still testing on every byte that would change. |
| **Row rejected on the cheap test before the expensive one** | **−23.2%** | **Portable, and the largest win measured.** zap's `match_row` has the identical structure and the same two expensive terms. See the note below: it contradicts the branchless guidance, which needs qualifying rather than discarding. |
| Short mnemonics packed into a word and compared in one operation | **+1.3% — reverted** | **Not portable, and not wanted.** Bucketing by letter and length already leaves one or two candidates, so the compare loop it replaced was two or three characters and building the packed key cost more. Recorded so it is not re-invented. |
| Constant shifts folded into lookup tables (`<< 3`, `<< 4`) | **−1.6%** | **Portable.** The eZ80 has no barrel shifter, so a shift that is not a whole number of bytes is a loop — `ld b, n; call __bshl`. zap folds operand indices into opcodes with the same `<< 3` and `<< 4`. |
| Immediate bytes read from the field rather than a local | **−1.5%** | **Portable.** zap's emitter writes the same one, two or three bytes and has the same choice about where to read them from. |
| Hot functions kept out of `main`, so every frame fits ix's range | **−7.8%** on pure, **−28.3%** on the row-heavy shape | **Portable, and zap has it worse.** See the section below. |
| Row data in one record per row, walked by a pointer | **−10.1%** on pure, **−35.0%** on the row-heavy shape | **Portable.** zap's `match_row` runs the identical test and reads `regsetA`/`regsetB` as `uint32_t` straight out of the isa table. |
| Register sets as separate byte planes | **+8.5% — reverted** | Recorded so it is not re-invented: it removes the right calls and replaces them with eight `ld hl, base; add hl, bc; ld a, (hl)` sequences per row. The same split *inside one record* is the row above. |
| Hex literals assembled a byte at a time, not `acc = (acc << 4) \| d` | **−2.6%** on six-digit immediates, neutral elsewhere | **Portable.** zap's `num_parse` accumulates the same way. Narrow: the compiler will not turn even `<< 8` into a byte move, so every hex digit was a call to `__ishl`, but that is a smaller share of a literal's cost than the shape timings suggested. |
| First letter to bucket base as a table, replacing a multiply | **−2.0%** on pure, **−2.4%** on `nop` | **Conditional**, on the same thing as the length buckets themselves — zap's lexer is context-free and does not know a statement start is a mnemonic. The *technique* is portable and the multiply is the point: `letter * NLEN` is a call to `__imulu`, because MLT is 8-bit and this is an int. |

## The frame-pointer cliff, and what zap has

`ix` displacement is a signed byte. A function whose frame is larger than 128
bytes cannot reach most of its own locals with `ld a, (ix-9)`, so the compiler
emits `ld bc, -139; lea hl, ix + 0; add hl, bc; ld hl, (hl)` instead — five
instructions where there was one, on every access.

dzap fell off it by accident. `run`, `assemble_line`, `match_row` and
`emit_row` were all inlined into `main`, whose frame reached 149 bytes and
whose hot loop paid the detour 23 times. Marking those four `noinline` split
one 149-byte frame into four of 60, 62, 19 and 20, removed every escape, and
made the whole program *smaller* — 2557 instructions against 2634, despite the
calls and returns it added. It was worth 28.3% on the shape that scans the most
rows.

**zap is in the same state and further into it.** Compiled the same way:

    src/encode.c    106 escape sequences,   1 function over 128 bytes
    src/parser.c    191 escape sequences,   6 functions over 128 bytes
    src/operand.c     0
    src/lexer.c       0

That is not an inlining accident — those functions are simply large — so the
fix is not the same `noinline`, but the cost is identical and it lands on the
two hottest files in the program. Splitting the big frames, or moving the
locals a hot loop touches into a small helper, is the shape of it.

The same compile shows the other half. encode.c calls `__land` ten times and
`__lcmpzero` twelve; parser.c calls `__imulu` eleven. The `l` prefix is the
32-bit helper: that is `reg_match` testing `uint32_t` register sets, the exact
code the row-record change above rewrote in bytes.


## Standing note

The host cannot see the `uint24_t` change at all -- `uint24_t` is a typedef for
`uint32_t` under the test stubs, so callgrind reports it as exactly zero
instructions. It was the largest single win measured. Any portability judgement
made from a host profile will miss that whole class of change; the verdicts
above are about *semantics*, and the sizes are always from the Agon.

## Running total

    256 KiB of pure instructions, dzap
      baseline                                    19.72s   1,387 cycles/byte
      + lengths, class table, length buckets      17.76s   1,250
      + row precompute, operand copy, literals    17.10s   1,203
      + 24-bit masks, dot in table                15.00s   1,055
      + 24-bit immediate, single-pass lines       14.30s   1,006
      + narrowed accumulators, reserve per insn   13.98s     983
      + cheap-test early-out in row selection     10.74s     756
      (round 7 tried and reverted -- see below)
      + shift tables, emit_imm, frames, row record 8.92s     627
      + hex literals, bucket base table         8.76s     616

The last line is one session's four changes, each measured on its own against
the same build of the same file. That round's baseline re-measured as 11.10s
rather than the 10.74s recorded above — about 3% of run-to-run drift between
days — so its four steps are quoted against 11.10s and against each other,
never against a number from another day:

    baseline                                     11.10s
      + constant shifts as lookup tables         10.92s   −1.6%
      + emit_imm reading the field               10.76s   −1.5%
      + hot functions out of main                 9.92s   −7.8%
      + row data in one record                    8.92s  −10.1%
      + hex literals a byte at a time             8.94s   +0.2%
      + first letter to bucket base table         8.76s   −2.0%

The hex change is neutral on this file and worth −2.6% on six-digit
immediates alone; it is in because it costs nothing elsewhere, not because it
showed up here.

On `ld (ix+8), a` alone, which scans 43 of ld's 57 rows and so shows row
selection undiluted: 42.54s to 19.76s, **−53.6%**.

Roughly two thirds of the 45.5% so far is portable or conditional; the rest is
the simplification.

## Round 7: both halves lost

| | 256 KiB | vs baseline |
|---|---|---|
| baseline | **10.74s** | |
| skip the first character in the mnemonic compare | 11.20s | +4.3% |
| ...and merge `row_modes` with `row_ccok` | 12.76s | +18.8% |

**Skipping a known-equal character costs more than it saves.** The bucket
already agrees on the first letter, so `same_ci(name + 1, s + 1, n - 1)` looked
free — but it adds two pointer additions and a subtraction per candidate to
remove one iteration from a loop that runs one to three times.

**Packing a flag into the spare bit of a `uint16_t` cost about 14%.** It
replaced a byte load and a 16-bit compare with a 16-bit load and two 16-bit
`AND`s. The guide's §1 says 16-bit is the worst width in ADL mode because the
architecture has no dedicated 16-bit truncation and the compiler must mask the
upper 8 bits of a 24-bit register. That is the obvious explanation and the
direction is right, but this measurement does not isolate it: the change also
removed a byte load and altered the comparison, so it is evidence for the
guide's entry rather than a clean verification of it. The entry stays marked
unverified.

Both reverted. Neither is portable, since neither is an improvement anywhere.

## Branchless is not unconditional

`ez80_advanced_optimization_guide.md` says data-driven beats chains of if/else
on a chip with no branch predictor, and that was measured and is true -- for a
branch that only **selects a value**. The row early-out **skips work**: one
compare of a precomputed value avoids two `reg_match` calls, on three rows out
of four. That is worth far more than the pipeline costs, and it was the single
largest win in this whole exercise at 23.2%.

zap's `match_row` was deliberately made branchless earlier in the project for
the reason that does not apply here. Carrying this over is likely the most
valuable single thing dzap has produced.
