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

Roughly two thirds of the 27.5% so far is portable or conditional; the rest is
the simplification.
