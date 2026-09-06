# Where zap stands, and what is left

Notes to self, 2026-09-05. Everything here is measured, not estimated, unless
it says otherwise. Timings are on fab-agon-emulator 1.2.4 without `-u`, which
decouples the guest clock from guest work and makes every number meaningless.

## Where we are

| | zap | ez80asm |
|---|---|---|
| big.asm (473 KB, 99 KB out) | **57.88s** | 59.32s |
| BBC BASIC (386 KB tree, 20.9 KB out) | **20.96s** | — |
| rokky | **2.40s** | — |
| big.asm peak memory | **158.2 KB** | 139 bytes (`-m`) |

Speed is done, or near enough: we were 1.62x slower in July and we are now
marginally ahead on the one source both assemble. That is the headline, but it
is also the less interesting half.

The memory column is the real gap and it is not close. ez80asm streams both
source and output and holds neither, so its peak is a rounding error and the
size of the program it can assemble is bounded by the disk. zap holds the
output, the symbol table and the pending fixups, so the program size is bounded
by 512 KB of SRAM minus whatever MOS wants.

## What the peak is made of

Measured with an allocation shim that snapshots per-site totals at the instant
of the global peak, so this describes one real moment rather than a sum of
maxima that never coexisted.

big.asm, 158.2 KB:

    parser.c   output buffer     128.0 KB   80.9%
    buf_reader source line buf    16.0 KB   10.1%
    hash_table symbols            13.5 KB    8.5%

BBC BASIC, 223.3 KB:

    label_stack fixup nodes        44.0 KB   19.7%
    buf_reader  source line bufs   32.0 KB   14.3%   (two, nested includes)
    parser      output buffer      32.0 KB   14.3%
    parser      macro bodies       28.0 KB   12.5%   (7 x fixed 4096)
    hash_table  symbols + keys     68.5 KB   30.7%
    label_stack expression arena   16.0 KB    7.2%

Two different programs, two different limits. big.asm is output; BBC BASIC is
everything else, fairly evenly.

## Next steps, in the order I would take them

### 1. ~~Lazy prescan~~ -- done, and better than planned

The prescan is gone entirely rather than made lazy. It existed for operands
whose value folds into the opcode byte -- rst, bit, im, set, res -- where there
is no hole to leave for a fixup. There did not need to be one: every folding
transform ORs its value in, and an unknown immediate is zero, so the byte
already written is exactly the base the value ORs into. Settling it later sets
the same bits.

Worth 2.0% on big.asm and 9.7% on BBC BASIC, and it made zap stricter in four
places where the prescan had made it wrongly accept what ez80asm rejects
(`ds SIZE`, `align X`, `fillbyte X`, `if COND` with forward constants).

The audit this was gated on turned out to be unnecessary -- the question was
whether a directive might silently substitute 0, and the answer is that the
folding path had an explicit `folds_known` check and everything else goes
through `expr_eval`, which errors. Worth remembering that the gate was real but
the cheaper answer was to remove the need for it rather than to satisfy it.

### 2. Output streaming — takes output out of the memory limit

This is the one that answers ez80asm, and it is *possible*: `parse_org` already
rejects a backward `.org` ("new address lower than current address"), so output
is written strictly forward. `mos_flseek` exists and buf_reader already uses it
for reading.

The obstacle is that fixup patches write backwards, and they reach much further
than expected:

| | patches | furthest back | output |
|---|---|---|---|
| BBC BASIC | 2,150 | 20,728 B | 20,883 B |
| rokky | 206 | 31,202 B | 31,520 B |
| snes | 36 | 853 B | 939 B |
| big.asm | 0 | — | 99,120 B |

In every real program something defined at the end is referenced at the
beginning, so a fixup reaches back to within ~1% of the start. And it is not one
outlier: 5.1% of BBC BASIC's patches reach past 16 KB, 24.6% past 4 KB.

So streaming has to be flush-and-seek-back, not flush-and-forget. A 16 KB window
would cost ~109 seek-patches on BBC BASIC, 1 on rokky, 0 on snes. Worth it —
output is 81% of big.asm's peak — and the seek cost, which this was gated on, is
**not** a problem. Measured on the emulator: writing 100 KB takes 6 cs, and 200
patches at offsets spread over the whole file -- 400 seeks, with
`FF_USE_FASTSEEK 0` -- take 2 cs, about 50 microseconds each. The patched file
verified byte-correct. BBC BASIC's 2,150 patches would cost roughly 0.2s against
a 21s assembly.

So the gate is open and this is the next big one.

### 3. Cheap memory wins not yet taken

- **Macro bodies are a fixed `malloc(4096)` each**, 28 KB for BBC BASIC's seven.
  Sizing to the actual body should recover most of it. `parser.c`, in the macro
  definition path.
- **16 KB source line buffer per open file.** Nested includes multiply it — BBC
  BASIC holds two at its peak, 32 KB. Worth checking whether a suspended
  include needs to keep its buffer at all, or can give it back and re-read on
  resume. It already seeks on resume.
- **The output buffer doubles.** 128 KB to hold 96.8 KB. Trimming happens at
  hand-over, but the peak is mid-parse, so it does not help the peak. Growing by
  something less aggressive than 2x near the top would.

### 4. Time, if we go back to it

`lex_next` is 30% of instructions and has been the floor through every round.
The remaining ideas, none measured:

- The prescan's inner loop compares each byte against three characters. A
  256-byte lookup table indexed by the byte would replace three compares with
  one indexed load, and on a 256-byte-aligned table the eZ80 does that in `ld
  l,c` / `ld a,(hl)`. This is the data-driven-beats-branches pattern that has
  paid every time we have tried it on this chip.
- `enc_instruction` is 15.5% on big.asm but only 7.9% on BBC BASIC, so it scales
  with instruction count rather than source size. `match_row` is already
  branchless.
- Hash lookups are 14-17%. 51.7% of them are registers, which are known at lex
  time. Resolving register names in the lexer rather than through the symbol
  table would remove half the lookups. This was scoped once and never tried.

### 5. A full corpus, and a fixed benchmark set

The 247-source corpus is the reference's own test suite: small, single-file,
none nesting includes, the largest a few hundred lines. Every interesting
failure so far has come from real programs instead -- a case-insensitive label
collision, a macro expansion that split a routine's locals, a scope not restored
across an include -- and there are only three of those in rotation.

Harvest every assembly program from https://github.com/sabotrax/agon-software
into a separate full corpus, then, in order:

1. Confirm ez80asm assembles them all. Anything it rejects is out of scope --
   zap is not trying to be better than the reference, it is trying to agree
   with it.
2. Confirm zap assembles them all, byte for byte. Expect failures; that is the
   point of doing this.
3. Pick a representative subset for benchmarking -- a range of sizes, macro
   density and include depth -- and pin it. Every timing figure from then on
   uses that set, so numbers stay comparable across months rather than being
   re-argued each time.

Keep it separate from `test/corpus`. That one is small, fast and runs on every
commit; a full corpus of real programs is a different thing with a different
cadence, and mixing them would make the fast check slow enough to skip.

## Tried and reverted: the operand fast path and lazy capture

Evaluating `0x42` the general way is eight nested calls to reach a value the
lexer already converted and left in the token, and `expr_capture` copied every
expression's text although 60.7% of them on BBC BASIC are never re-read. Both
looked like clear wins. Measured on the Agon, neither was:

| | bbcbasic | rokky | synth |
|---|---|---|---|
| main | **18.42** | **2.28** | 38.06 |
| lazy capture + split eval_at | 18.90 | 2.30 | 36.92 |
| eager capture + split eval_at | 19.04 | 2.34 | 37.06 |
| lazy capture + eval_at parameter | 18.86 | 2.30 | **36.90** |

Every variant is slower than main on both real programs and faster only on the
synthetic one. `synth` is made entirely of literal operands, so a literal fast
path helps it and nothing else; real sources reach for symbols, where the fast
path never fires and its machinery is pure cost.

Two hypotheses about *why* were both wrong, and both took a target run to
disprove. First that the lazy capture was the expensive half -- it is the
cheaper one, beating eager on all three. Then that splitting `eval_at` in two
put a call on every expression -- replacing the split with a parameter moved
BBC BASIC by 0.04s. The cost is spread through the added machinery, not in any
one thing worth naming.

**Do not retry this shape without a real source that is dense in literal
operands.** If one turns up in the full corpus (see below) it would change the
answer; `synth` alone is not evidence, being a file this project wrote to
exercise the encoder.

What survived is in ed16510: an uninitialised filename in `resolve_fixup`, and
tests for deferred expressions, ADL widths and an include ending mid-expression
-- all of which cover behaviour that already existed and had nothing pinning it.

## Things that were measured and turned out not to matter

Recording these so they are not re-litigated:

- **`ls_retire`'s bucket walk.** Made O(1) with a back-link. Zero measurable
  change: chains are ~4 long, so it was ~4,000 scattered reads, milliseconds.
  Kept only because it bounds the worst case for 3 KB.
- **The fixup index's cost is not extra evaluation.** 2,140 resolutions before
  and after; `post_process` now finds nothing. The 3.7% on BBC BASIC is the
  per-statement hash on 1,896 label definitions plus 5,875 bucket visits, both
  inherent to indexing.
- **Single-pass hashing.** 87.2% of hash calls already took the narrow path.
- **The scanner rewrite** (line-buffer redesign) was 6-9% *slower* and was
  abandoned along with two rescue attempts. It added a copy without removing a
  pass.

## Measurement hazards, all of which have bitten

- `-u` on the emulator decouples the guest clock from guest work. Never measure
  with it.
- Two emulators sharing one sdcard directory mutate the filesystem under each
  other and produce an `RST $38` guru meditation that looks exactly like a zap
  bug. The runner scripts refuse to start if one is already there.
- `pgrep -c agon-cli-emulator` always returns 0: the process name is truncated
  to 15 characters. `pgrep -f <pattern>` matches the shell carrying the pattern.
  Use `pgrep -x` and read `/proc/<pid>/cmdline`.
- Whether the output file already exists perturbs repeats. Remove it first.
- A variant whose patch failed to apply builds an unmodified binary and measures
  identically. Check the whole log, not the numbers.
- A reference binary that cannot run reports every source as a divergence and
  looks exactly like zap having broken. A relative path to ez80asm resolved to
  nothing from inside the per-source work directory and turned 247 passes into
  247 failures with no error printed anywhere. corpus.sh now assembles one
  known-good source first and refuses to go on if the reference produces
  nothing.
- Host instruction counts understate eZ80 gains wherever libc is vectorised.
  The prescan change was 4.0% on the host and 1.8% on target — same direction,
  wrong magnitude. Target numbers are the real ones.

## Still open, unrelated to performance

- README: what zap is, how to use it, and a zap-vs-ez80asm compatibility section
  in both directions. Divergences collected so far: zap accepts lines >256 chars
  where ez80asm errors; zap caps captured expressions at 128 chars and ez80asm
  has no limit; zap caps macros at `MACRO_MAX` (64) where ez80asm chains them;
  `db L` with a forward `equ` fails in zap without the prescan; `.DS 10, 0`.
- `bin/zap.bin` is tracked in git and changes on every build. Decide whether it
  belongs there or in `.gitignore` with a release artifact instead.


## Open threads, as of the register-operand round

**The buf_reader sentinel is stashed, not abandoned.** A newline written one
byte past the buffer's last valid byte lets every scan drop its bound: 16 loops
and a dozen single tests, and `e` comes off both `parse_operand` and
`assemble_line` entirely. It measured **-3.9% on `ld a, b`** before it broke.

It fails on the Agon and passes everything on the host, including ASan at four
different buffer sizes. What is known:

- `sh_reg.s` assembles correctly with it; `p256.s` fails at line 2, which is
  `ld a, 0x42`. `sh_reg.s` line 2 is `ld a, b`. That points at the literal
  path, not at refilling.
- The guest says `unexpected text after the instruction`, which comes from
  `run()` finding `stop` somewhere other than the newline.
- It is not disk, and the host reproduces nothing at BUF_KB of 1, 2, 4 or 16.

That run has now been done, and it narrowed things a long way without settling
them.

**A four-line file fails at line 2.** So it is not refilling: the whole source
is 41 bytes, one buffer, and line 2's scan ends on a real newline at offset 18,
nowhere near the sentinel at 41. The sentinel is not even reached.

**The same file assembles correctly on merged HEAD.** So the fault is in this
change, not in anything already on main.

**The class table is right on the target.** Printed from the guest:
`cclass['2']` is 0x1E and `num_ch('2')` is 1, so the scan has no reason to stop
where it does -- and the diagnostic says it stops at offset 17, the final `2`
of `0x42`, having consumed `0x4` and left `2` as trailing text.

**It fails at `-O1` as well as `-Oz`,** so it is not one optimisation level.

**Adding a `printf` inside the literal path makes it pass** -- with the
diagnostic in, the target produces `n=4 v=66` and the right bytes, identical to
the host. That is the signature of a codegen or layout problem rather than a
logic error, and it is why reading the C has not found it: the C is the same
code that works on the host at four buffer sizes under ASan.

Where to pick it up: the literal scan compiles to `.LBB3_144` and looks correct
by hand, but the *other* `num_ch` loop in the same function -- the displacement
scan at `.LBB3_18` -- pre-increments its pointer and then tests `(iy + 1)`,
reading one character ahead of where it has advanced to. That is worth
understanding before anything else, even though the failing instruction does
not take that path.

The honest position is that this is a heisenbug worth an hour with the
disassembler and a hardware single-step, not another round of reading the
source. It buys 3.9% on one shape. It is stashed, not lost.

It also found one real bug on the way in, already fixed in the stash: writing
the sentinel at `bsz_` clobbers the first character of the partial line the
reader carries to the next buffer. That produced wrong output with no crash,
ASan clean, and all 96 checks passing -- every case file is smaller than one
16 KiB buffer, so none of them ever reached a refill. `dzap/test/cases/refill.s`
(in the stash) places a line across each of the first three boundaries and
catches it.

**Where the remaining time is.** `ld a, b` at 4,436 cycles divides as: read and
line loop 676, mnemonic scan 283, `mnemonic_of` 393, both `parse_operand` calls
1,757, `match_row` 602, `emit_row` 725. `parse_operand` is 40% and is the
obvious next target; the sentinel was an attempt at it.

**Two things measured and deliberately left alone:** shrinking the `dop` struct
(a copy is an `LDIR`, about 25 cycles, so there is nothing to win) and
splitting `parse_operand` so the empty case skips its prologue (+2.0% -- the
caller already assigns `dop_none` directly when there is no comma, so the
early-out is rarer than it looks).


## The parse_operand entry stage, and why the merge is not in

The entry -- the call, the frame, clearing the operand, finding where it starts
-- was the largest single piece left of `parse_operand`. It was priced directly,
by adding a third call per instruction on a source that takes the empty path:
**about 430 cycles**, on both `nop` and `ld a, b`. Two calls per instruction, so
merging them into one "parse the operand list" function should be worth one of
those.

It was built three ways and all seven shapes measured.

| shape | main | merged, inlined | merged, out of line |
|---|---|---|---|
| `ld a, 0x42` | 9.66s | **9.10s (−5.8%)** | |
| `nop` | 5.44s | **5.14s (−5.5%)** | 5.58s (+2.6%) |
| `ld hl, 0x123456` | 11.96s | **11.36s (−5.0%)** | |
| `bit 3, (iy+4)` | 11.08s | **10.86s (−2.0%)** | |
| 256 KiB mix | 6.54s | **6.44s (−1.5%)** | |
| `ld (ix+8), a` | 14.32s | 14.70s (+2.7%) | |
| `ld a, b` | 6.98s | 7.22s (+3.4%) | 7.62s (+9.2%) |

Keeping it out of line is worse than either -- the whole win depends on the
compiler then inlining the merged function into `assemble_line`, which it does,
taking that frame from 56 bytes to 83. Still short of the 128 where ix
displacement stops reaching, so that is not the problem.

**Not taken, despite the mix improving.** The two regressions are exactly the
shapes whose second operand is a register; every shape with an immediate or no
second operand wins. Register-to-register is the commonest form in real code,
and `ld a, b` was confirmed at 7.22s on two separate runs.

The deciding point is that the mix cannot arbitrate this. `p256.s` uses **31 of
the ISA's 114 mnemonics** and 40 of its 322 rows: no `call`, `jp`, `jr` or
`djnz`, so condition-code rows are barely exercised at all; no block
instructions beyond `ldir`; none of `lea`, `pea`, `in0`, `out0`, `tst`. Its
1.5% gain is an average over a sample that happens to over-weight the shapes
this change helps. Banking that against a 3.4% loss on the commonest real form
is not a trade worth making on this evidence.

**Built that benchmark, and it reversed the verdict.** `gen_isa.sh` produces
two 256 KiB sources holding all 1,083 forms -- one giving every form equal
weight, one weighted like the real programs in test/corpus. On both, the merge
is *slower*:

    isa_even   main 7.02s   merged 7.08s   +0.9%
    isa_real   main 6.60s   merged 6.68s   +1.2%

Three interleaved repeats of isa_real read 6.60 and 6.68 every time, so that is
real. The 1.5% gain on p256 was the forty-form sample over-weighting the shapes
the change helps. **Rejected outright**, not merely deferred.

**Why the register shapes regress, and why the obvious fix is worse.** In the
original, `op` is a parameter: loaded once into iy, every field store a cheap
`(iy+n)`. Merged, `op` alternates between the two operands and becomes
loop-carried -- `ld iy, (ix + 9)` falls from 49 sites to 19 and address
arithmetic rises from 55 to 61. The register path does the most field stores of
any path, which is exactly why it is the one that slows down.

Parsing into a fixed local and copying out once should undo that, and it does
undo the *symptom*: `lea` sites fall back to 57. It is catastrophic anyway.

    isa_real   main 6.60s   merge+tmp 8.00s   +21%
    isa_even   main 7.02s   merge+tmp 8.68s   +24%
    ld a, b    main 6.98s   merge+tmp 9.70s   +39%

Two struct copies per instruction should be about 50 cycles by the earlier LDIR
measurement, and this costs thirty times that, so the copies are not what is
expensive -- something about the shape defeats the compiler more broadly. Not
worth chasing further: the entry stage resists this whole line of attack, and
three variants have now been measured against it.

**Where that leaves the entry stage.** Its ~430 cycles are real and are the
largest single piece of `parse_operand`, but they are not recoverable by
restructuring the call. Anything that removes the call changes what the
compiler inlines and how the operand pointer is addressed, and every version
tried has paid more for that than the call cost. A different angle is needed --
not another arrangement of the same two calls.

A methodological note worth keeping: **pricing a call by adding one does not
predict what removing one saves.** The 430 cycles were real and reproducible on
two shapes, and the merge still cost time on two others, because removing the
call also changed what the compiler inlined and how the register path came out.


## The operand parse, and why it resists

It is the largest single stage on `isa_real` at 1,433 cycles an instruction,
28% of everything, and three ways into it have now been measured and closed.

**The entry is 287 cycles a call**, priced by adding a third call per
instruction on a source that takes the empty path. Two calls, so about 574 of
the 1,433 is entry: the call, the frame, clearing the operand, and finding
where it starts.

**Merging the two calls into one.** Tried, and it is a regression on the
benchmarks that hold the whole instruction set -- +0.9% and +1.2% -- despite
looking 1.5% faster on the forty-form file. Merged, `op` becomes loop-carried
and the cheap fixed-parameter load disappears; parsing into a fixed local and
copying out once addresses that and is 21% to 39% slower still.

**Dropping an argument.** `e` is used only by the two bounded scans and does not
change for a whole buffer, so it can be a file-scope pointer instead of the
fourth argument to a function called twice per instruction. It works: −0.4% on
isa_real, −0.7% on isa_even, both above the noise. **Not taken.** dzap is meant
to become zap, which assembles includes, and one global buffer end is wrong the
moment there is a nested reader. A global that has to be un-globalised later is
a poor trade for half a percent.

**Skipping the call for instructions with no operand.** Only 7.6% of lines in
isa_real have none -- 1,660 of 21,869 -- so at 287 cycles a call the ceiling is
44 cycles an instruction, 0.9%, before the cost of testing for it on the other
92%. That is why the earlier attempt measured +2.0%: the test costs more than
the saving. Quantitatively dead, not merely unpromising.

What is left is structural. The 574 cycles of entry are two calls with two
frames, and every way of having fewer has cost more than it saved. Something
that changed the shape -- parsing both operands in one pass over the line
without a per-operand call, without a loop-carried destination pointer -- is
the only thing not yet tried, and the two attempts closest to it both lost.
