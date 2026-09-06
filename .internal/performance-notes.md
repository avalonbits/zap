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

## Row selection, and what it cost to find out

Round taken 2026-09-06, on the two 256 KiB whole-instruction-set sources.
`isa_real` went 5.34s to 5.06s and `isa_even` 5.70s to 5.36s, 5.2% and 6.0%,
output byte-identical throughout. At 5.06s over 262,144 bytes that is **356
cycles per source byte and 50.6 KiB/s** on `isa_real`, from 376 and 47.9; and
377 cycles per byte and 47.8 KiB/s on `isa_even`, from 401 and 44.9. The two
hand-built sources moved with them: `p256` 5.38s to 5.18s and `pure` 10.64s to
10.26s, both 3.7%.

**Counting first.** An instrumented host build over `isa_real` said 4.08 rows
examined per instruction, of which **3.40 reached the register test** and only
0.68 were disposed of by the mode test. The comment in the source claimed the
opposite -- "all but one are rejected" -- and had been true before the rows
were grouped by mode. The rows an instruction wastes time on are rows of the
right shape with the wrong registers.

Per mnemonic, `ld` is **62% of every register test in the file**: 5,502 of
21,862 instructions, 8.38 rows each, 57 rows in 7 mode groups. Nothing else is
above 4.4%.

**What worked.**

* Testing A alone and reaching B only if A survived: **2.2%**. A rejects 2.00
  of the 3.40, so B is not computed for three rejections in five. The two 0/1
  values that were ANDed together also stopped being materialised.
* Lifting the modes into a group table: **1.9%**. Rejecting a mode used to cost
  a whole turn of the row loop -- counter test, row pointer into `iy`, mode,
  ccok, skip, next -- and is now a compare and a five-byte step. Rows reached
  through a group carry no mode test at all.

**What did not.** A class-refined mode, splitting each group by which register
class the operand is in (r8 / r16 / index / index halves / I,R,MB). Simulated
against the real walk with zero mismatches: full tests fall 3.40 to 1.11, but
group rejections rise 0.68 to 5.75 and eat it. Refining only the B side is the
best of the three variants (1.79 full, 2.22 rejections) and is worth about 2%
against the group table -- not taken, for a class table, a duplicated row and a
per-instruction code computation. The numbers are here if it is ever worth
revisiting; the simulation harness is the one thing worth rebuilding first.

One row of 322 spans two register classes (A = IX|IY, B = BC|DE|IX|IY), which
is why any such scheme needs a row to be enterable from more than one class.

**Where the time is now.** The stage decomposition was taken at 435 cycles per
byte and said: read line 14%, classify mnemonic 6%, `mnemonic_of` 15%, parse
operands 28%, `match_row` 27%, `emit_row` 10%. Both of the last two have since
been cut, so the operand parse is now the largest thing left by some margin,
and it is the stage with the most closed avenues -- see the section above.

## Where the cycles are now, measured stage by stage

Taken twice on `isa_real`, once at 4.80s and again at 4.52s after the last two
inlines, by building six variants of `assemble_line` from one source with a
compile-time `STAGE` and timing each. Every variant, **including the full one**,
ends with the same scan-to-newline tail, so no difference between two of them
contains it; the tail costs 0.10s and is taken off the reader's share. The
stages sum to the total exactly, which is the check that the method holds.

    isa_real  4.52s = 3,810 cycles/instruction, 318 cycles/byte, 56.6 KiB/s

    stage                       seconds  cycles/ins   share   at 4.80s
    read the line + dispatch       0.64         540   14.2%      0.66
    scan the mnemonic              0.24         202    5.3%      0.22
    mnemonic_of                    0.84         708   18.6%      0.84
    parse both operands            1.06         894   23.5%      1.06
    match_row                      0.62         523   13.7%      1.08
    emit_row                       1.12         944   24.8%      0.94

**Read the split between adjacent stages with care.** The totals are exact but
the attribution is not, because a variant that omits a stage also has a smaller
frame and a different register allocation, so the growth lands on whichever
delta introduces it. That is why `emit_row` appears to have grown from 0.94 to
1.12 in a round that only moved it inline: the 82-to-94 byte frame growth is
charged to the delta that causes it. `match_row` falling 1.08 to 0.62 is real
-- it is this round's work -- but part of the drop is `match_row_cc` being
inlined and its cost moving.

**What has not moved at all is `mnemonic_of`: 0.84s in both measurements**, now
18.6%. It is the stage that has resisted two rounds of attention. A better hash
was tried and lost 5.7%; packing short names into a word and comparing them in
one operation lost 1.3%. Bucketing by first letter and length already leaves one
or two candidates, so what is left is not the comparison but the getting there.

**The four remaining calls per line are gone.** `parse_operand`, `emit_row` and
`match_row_cc` were all inlined this round for 5.1%, 3.8% and 2.2%, and nothing
in the hot path is out of line any more. `assemble_line`'s frame is **94 bytes**
against the 128 that `ix`'s signed displacement allows -- that is the budget,
and it is the number to re-read before adding anything to the function or to
`dop`.

## emit_row, and two prices for the same stage

The staged decomposition put `emit_row` at 1.12s, 944 cycles an instruction and
the largest stage in the program. Pricing it a second way says 0.54s: build one
that emits twice, rewinding the cursor in between, and take the delta against
the real build. Output stays byte-identical, which is the check that the rewind
is honest.

    staged (s4 -> s5)       1.12s   944 cycles/ins   25.7%
    marginal (emit twice)   0.54s   455 cycles/ins   12.4%

Both are true and they answer different questions. The staged number is
everything that appears when the stage is added, including the frame growing
from 82 to 94 bytes and the register allocation of the whole function changing.
The marginal number is what one more execution costs when the pointers are
already in registers and the reserve already has room -- a floor, because it
does not pay for anything the two executions share.

**The real cost is between them, and nearer the lower one.** Which matters,
because it reorders what to do next: `emit_row` is not the biggest stage. The
operand parse at 1.06s and `mnemonic_of` at 0.84s both are, and neither has a
gap between the two ways of measuring, because neither changed the frame.

**Use the marginal method to rank, the staged method to account.** The staged
numbers sum to the total and the marginal ones do not, which is exactly the
trade.

### What this round took out of it

* `ddfd_prefix` was `static inline` and the compiler said no -- at -Oz the hint
  loses to size. Two real calls with frames, for the 42% of instructions whose
  row allows an index register. Forcing it inline: **3.1% and 2.9%**, the best
  single line of the round, for 278 bytes.
* The output held as pointers rather than a base and two offsets: 0.5%. The
  reserve was `pos + 12 > cap` on two signed ints, which is eleven instructions
  and a `call pe, __setflag` to fix the flags up on overflow; against a pointer
  limit it is seven and no call.

### What is not worth doing to it

A fast path for instructions that need no prefix, no displacement, no transform
and no immediate. Counted: **13.6% of isa_real and 3.8% of isa_even**. The test
would run on the other 86% and 96%.

`transform` is not the exception either -- it runs for 64% of isa_real and 75%
of isa_even, so the `!= TR_NONE` test before it is buying less than it looks.

## The mnemonic lookup, and the operand parse holding out again

`mnemonic_of` had not moved across two measurements -- 0.84s both times -- and
had two rounds of attention behind it. It moved this time, twice, and both were
in the getting there rather than the comparing.

    isa_real 4.36s -> 4.16s, 318 -> 292 cycles/byte, 56.6 -> 61.5 KiB/s
    isa_even 4.68s -> 4.42s, 340 -> 311 cycles/byte, 52.9 -> 57.9 KiB/s

**Both of bucket_of's steps were calls.** The clamp `n < NLEN ? n : NLEN - 1`
is a *signed* compare, which is eleven instructions and a `call pe, __setflag`
to fix the flags up on overflow. And `bucket_head[b]` is a subscript on an
array of pointers, so it is b times three, and three is a call to __imulu.

A token of eight characters or more is not a mnemonic, so the clamp became an
unsigned rejection. The multiply needed the element size to change: **every
portable way of writing the subscript keeps the call** -- byte index, unsigned
index, scaled pointer arithmetic, all measured -- and what removes it is a
power-of-two size, so the slot carries a pad byte. Worth 1.8% and 2.1%.

`b + b + b` on a cast pointer also removes it and is wrong: a pointer is eight
bytes on the host, where the tests run. That cost a round trip through a failing
host test to find.

**same_ci was re-deriving what the bucket had settled.** It compared character
zero, which letter_base has already matched case-insensitively, and it folded
both sides when every name in the table is lower case. At 4.15 characters
compared per lookup that was a fifth of the work. Worth 2.8% and 3.5%.

The chain itself is not the problem and never was: **1.44 candidates per lookup**
on isa_real, 2.09 on isa_even.

### A shift is a call too

Removing the __imulu put a `call __ishl` in its place -- the backend lowers even
a constant shift by two to a helper. Writing it as `b += b; b += b` does not
help: LLVM canonicalises the adds back into a shift first. Any index-to-pointer
scaling costs one such call, so the win available was trading the expensive
helper for the cheap one, and that is what the 1.8% is. Getting to zero needs a
stride of one byte, which means storing indices, which needs a scale to turn
back into a pointer. There is no arrangement without one.

### The operand parse, two more closed doors

Counted over isa_real: **82% of operands are registers**, 13% immediates, 15%
parenthesised, 5% nothing. Of the immediates, 66% hex and 30% decimal, and
**0.00% are negative** in either corpus.

* **A negate the compiler cannot speculate: +1.0%.** `v = neg ? -v : v`
  if-converts to an unconditional 24-bit negate -- a call to __ineg, and there
  is no form of it that is not; `0 - v`, `~v + 1` and an explicit if all
  compile the same. Moving it into a `noinline` helper does produce
  `call nz, _negated`, so the common path pays one flag test instead of a call
  -- and it is *slower*, because the allocator must spill around a call it
  cannot prove will not happen. The spill costs more than the call it avoids.
* **Holding `indirect` in a local: +0.5%, flat on isa_even.** The struct field
  was already in a register; the local only added a slot.

The parse resists for the reason it resisted before. Its cost is 82% register
operands, and that path is a character scan, a switch and five stores, with
nothing in it that is a call or a re-derivation.

## The corpus was lying, and what it cost to find out

`opcodes.s` was regenerated by "filtering the corpus through dzap and keeping
what it accepts", which made it a record of what dzap already got right. A form
dzap assembled wrongly was removed by the very filter meant to catch it.

Rebuilt with **ez80asm as the only oracle**, it immediately found **53 wrong
forms** in three kinds:

* **40 hexadecimal literals with a trailing h.** They begin with one of a..f,
  so the operand looked like a name, reg_of_text failed, and the parser said
  "unknown operand" -- while num_parse, which has always understood the suffix,
  was never reached. Rewinding to the start of the token is the whole fix.
* **12 lea and pea forms.** Both take a displacement on a bare register -- their
  rows ask for NOREQ with F_DISPA or F_DISPB, not INDIRECT -- and the parser
  looked for one only inside parentheses.
* **`ex af, af'`.** The table holds the shadow accumulator as plain R_AF.

**The benchmarks inherited the lie**, because gen_isa.sh draws its forms from
that file. So neither distribution had ever executed the branch that negates a
literal, and neither contained a trailing-h literal. Regenerating moved
isa_real from 4.16s to 4.98s: not a regression, a workload that had been
quietly excluding the forms dzap was worst at. **Every timing before that
commit is against different input.**

The honest workload then showed the cost immediately -- 46% of every immediate
in isa_real went through num_parse -- and sharing the byte-at-a-time hex
assembly between `0x` and the trailing h took it to 1%, worth 7.6%.

### What the reference actually accepts

Checked against ez80asm, not a manual, because the answer is not what one
assumes:

    hexadecimal   0x42   42h   42H   $42   #42
    binary        0b1010 1010b 1010B %1010
    decimal       66     010   0100  08

**There is no octal, in any spelling** -- 777o, 777q, 0o777, 0q777 all refused
-- and **a leading zero is decimal**: 010 is ten, 0100 is a hundred, and 08 and
09 assemble, which octal would refuse. No radix letter leads either: b1010,
o777, q777, h42, @777, &42 all refused. dzap already agreed on all 36 probes;
what was missing was any coverage, which is now in both the corpus and the
encoding tests.

## Reading the line: 15% of the program, and mostly unreachable

The stage is about 0.78s of 4.62s, and roughly 0.2s of that is the
scan-to-newline the measuring variant adds, so **the reader and dispatch are
around 12%**.

* **Ask for the newline before skipping trailing space: 1.7% and 2.1%.** The
  skip loop is expensive to *enter*: the compiler rotates it so the pointer is
  stored to the frame and shuffled through two register moves to read one byte,
  sixteen instructions to discover there is no trailing space. One compare
  replaces them.
* **Step to the next line without testing the buffer end: 0.4%.** The guard
  protected a step that cannot go wrong, given the sentinel.

### Inlining is not monotonically good

Three of the four inlines tried this round lost, which is worth stating
plainly after three earlier ones won 5.1%, 3.8% and 3.1%.

* **assemble_line into run: +17.6%.** The frame fits -- 88 bytes, less than
  assemble_line's own 100, because the compiler shares slots -- and it is still
  the worst change measured all session. With the whole body inside the loop,
  the loop-carried pointers spill.
* **hex_digits: +3.0%.** A large body called on 16% of operands. ddfd_prefix
  won 3.1% being three byte tests called on 42% of instructions; the shape of
  the callee matters more than the call count.
* **A `noinline` negate, to stop the compiler speculating a libcall: +1.0%.**
  The allocator must spill around a call it cannot prove will not happen.

The rule that fits all seven: inline a *small* body on a *hot* path, and
measure anything else.

## Labels: the first feature priced

Added 2026-09-06. dzap now has label definitions, references, forward
references and the errors the reference gives. `$` and `foo+2` are expressions
and are not in it.

    isa_real     4.52s -> 4.56s     +0.9%    (no labels in the source)
    isa_even     4.70s -> 4.70s      0%

    labels.s     6.62s   262,177 bytes   465 cycles/byte
    nolabels.s   3.40s   181,197 bytes   346 cycles/byte

**0.9% where labels are not used, about a third more per byte where they are.**
The first number is the design working: a label is the colon and not the
column, so a line without one pays a single test on the first token.

**One pass, patched at the end.** A forward reference cannot be resolved where
it is read, so the bytes go down as zero and a fixup list patches them when the
source runs out. A second pass would have cost most of the program again and
would have hidden what labels are worth.

### What the naming style costs, which is more than expected

The first version of the benchmark numbered one stem -- `lbl_routine_body_0001`
and up. Those share a first character, a length, and a last character drawn
from ten digits, and the symbol key is exactly those three, so **2,161 labels
went into ten buckets and the file took 20.82s instead of 6.62s**.

That is the tail the plan warned about, met in the first hour of having a
symbol table. Kept as `gen_labels.sh <bytes> same` so it can be watched. A hash
would not care; the key was chosen because it touches 11.8 characters per
lookup against Pearson's 17.5 *on average*, and this is what the average hides.

### Where the label cost actually is

Four sources of **identical size and line count** -- 262,191 bytes, 5,580 lines
-- differing only in label content, so a difference between two of them is the
labels and nothing else. Names chosen so the first character, the last and the
length all vary independently, which is the case the symbol key is good at.

    no labels at all                 1.60s
    + 699 definitions                1.80s   +0.20   5,274 cycles a definition
    + 1,394 backward references      1.94s   +0.14   1,851 cycles a reference
    + 1,394 forward references       2.00s   +0.20   2,644 cycles a reference
    the fixup machinery alone                +0.06     793 cycles a fixup

**It is the definitions, not the lookups and certainly not the fixups.** A
definition costs about three times a lookup and nearly seven times a fixup.
The one-pass design is vindicated: recording a forward reference and patching
it at the end is 793 cycles, which is nothing next to the rest.

A definition is a lookup plus an insert -- the name copied into the arena, the
node filled in, the bucket pushed -- and the insert is where the time goes.
Shortening what a definition copies is the obvious place to look next.

### But the key's distribution dominates all three

The same 699 definitions, same file size, same line count, only the *names*
changed:

    names spread over first, last and length      1.80s
    fifteen characters, first and last from a
      thirty-word list                            1.90s
    four characters, one leading letter           1.76s
    twenty-six characters, all `q...z`            6.06s

The last is 699 labels in **one bucket**, because the key is the first
character, the last and the length, and all three are constant. It is 26 times
the per-definition cost of the spread case, on a file that differs in no other
way.

That is a much sharper number than the plan's projection -- it estimated a
worst chain of 36 against Pearson's 17 and expected the average to carry the
decision. Met in practice the tail is not 2x, it is 26x, and it is reachable by
a naming convention rather than by malice: `lbl_0001` upward does it.

### So the two keys were measured against each other, and the plan was wrong

Seven sources, identical size and line count, both keys into the same 2,048
buckets:

    source                       structural   Pearson
    no labels at all                  1.60s     1.58s
    spread names, definitions         1.82s     1.72s
    spread names, backward refs       1.94s     1.96s
    spread names, forward refs        2.00s     2.02s
    four-character names              1.76s     1.60s
    fifteen characters, word list     1.92s     1.72s
    clustered names                   5.98s     1.84s

**Pearson is 1% worse on the two rows the structural key is best at and better
everywhere else** -- 5 to 10% on ordinary names, 69% on the clustered one. One
percent against a factor of three is not a close decision, and dzap now uses
Pearson. Both keys stay behind `DZ_SYMHASH`.

The structural key was given its best shot first. It was neither inlined nor
call-free -- `call __ishl` for the multiply, `call pe, __setflag` for a signed
length test -- and fixing both moved it by nothing. **The key computation was
never the cost.** The chain walk and the insert are, which is why the argument
that won the plan ("a hash is a walk, and the scan has already walked it")
priced the wrong thing.

Three things the comparison turned up, all invisible to a correctness test
because a bad hash is still correct:

* **A linear permutation is a poor Pearson table.** `i * 167 + 13` visits every
  value and leaves the rounds correlated: 234 of 2,048 buckets against a
  shuffle's 602. The first comparison ran with it and Pearson still won by
  three times.
* **Building the table in main was wrong.** The unit tests call `build_tables`
  and `build_cclass` directly, so they ran with a table of zeros -- every name
  in one bucket, correct and quietly quadratic. It belongs where the other
  tables are built.
* **Zeroing the table, dropping the second pass and moving the build all fail
  zero encoding checks.** What is testable is the distribution, and that is now
  asserted directly: 700 clustered names must reach more than 400 buckets with
  no chain longer than 6.

### Two bugs the feature brought, both target-only or nearly

* **`_` was a name character but not a number character**, so the literal scan
  -- where a name that is not a register ends up -- stopped at the first
  underscore. Real labels are full of them.
* **The leading space skip had to be bounded.** Unbounded it compiled to a loop
  rotated wrongly: pre-decremented, testing one character past the pointer, so
  the first was never examined. Same fault the `num_ch` scans already carry a
  bound for. It appeared only when labels were added around it, nothing about
  labels touches it, and it broke **every line on the Agon while the host build
  was perfect**. `dec iy` before the loop head is the tell.

### And two rebases removed rather than tested

Names were pointers into a realloc'd arena and symbols were an array, so both
needed rebasing on growth -- and neither rebase can be tested: glibc extends in
place, so the block does not move and deleting the rebase fails no check even
with six hundred long labels. Names are held as offsets and symbols in blocks
that never move. Code that only runs under a different allocator is code
nothing here can hold to account.


## The ISA benchmarks now contain labels

Changed 2026-09-06. They had none, so they could say what labels cost a source
that does not use them and nothing else. One definition and one reference every
eight lines -- the rate counted over the two real programs -- with the
references split evenly between backward and forward: 969 and 971 on the
generated isa_real.

    isa_real   4.88s   262,170 bytes   343 cycles/byte   52.5 KiB/s
    isa_even   4.98s   262,192 bytes   350 cycles/byte   51.4 KiB/s

**Every timing before this is against different input.** The previous numbers
-- 4.60s and 4.76s, on the same code -- were on files with no labels in them.
The step from those to these is what labels cost this workload: **6.1% and
4.6%**, which is the honest figure now that the benchmark exercises the
feature.

The forms were interleaved with the label lines rather than having their
operands rewritten. Replacing the operand of a `jp` with a label would have
kept the line count and lost the form, and containing every form is what these
files are for; all 1,019 are still present.


## The degenerate case, and what it says about the fixup design

`gen_isa.sh degenerate` is the worst shape a one-pass assembler with a fixup
list can be given: the first half of the file refers to L1..LN five times over
and the second half defines LN..L1, so

* **every reference is forward** -- none can be resolved where it is read
* **the fixup list reaches its largest and stays there** until the source runs
  out: 2,340 entries, none discharged early
* **the distance between a use and its definition is as long as the file
  allows** -- L1 is referenced 10,943 lines before it is defined

468 labels, five uses each, definitions in the exact reverse of first-use
order. All four properties checked rather than intended. Assembles
byte-identically to ez80asm.

    isa_real         4.88s   343 cycles/byte
    isa_degenerate   4.94s   347 cycles/byte     +1.2%

**1.2%**, which is the fixup design holding up. Recording a forward reference
costs 793 cycles and patching it later costs the same whether the definition is
one line away or eleven thousand, because the fixup carries the output offset
and the symbol and neither is searched for again. Interning is what makes the
patch a dereference rather than a lookup.

What the file is for is the *shape* of the cost rather than the size of it. If
resolution ever stops being linear in the number of fixups -- a search per
patch, a rebuild per growth -- this is where it shows first, and 1.2% is the
number to watch it against.

## Memory, which had not been measured since labels arrived

`test/bench/memprofile.sh`, on the Agon, over 262 KB of source:

    isa_real          peak 166,931 bytes      isa_degenerate   137,221
      source reader         16,385              source reader   16,385
      output buffer         65,542              output buffer   65,537
      symbol buckets         8,192              symbol buckets   8,192
      symbol blocks         22,540              symbol blocks    5,635
      label names           40,960              label names      8,192
      fixups                13,312              fixups          33,280

**Labels are 85 KB of the 167 KB peak, 51% of it.** The feature that costs 6%
of the time costs half the memory, which is the sort of thing that stays
invisible while only one of the two is measured.

**Worst for time is not worst for memory.** isa_degenerate is the worst case
for the fixup list -- 33 KB against 13 -- and uses *less* memory overall,
because it has 468 distinct labels against isa_real's 1,941 and the names and
symbol blocks scale with that. A memory-degenerate source is one with many
distinct labels, not many references, and there is not one yet.

### What it scales to

Everything but the reader grows with the source. At isa_real's label density:

    a  256 KB source peaks near 167 KB   fits
    a  512 KB source peaks near 317 KB   fits, with MOS to pay for as well
    a 1024 KB source peaks near 619 KB   does not fit

The output buffer alone is `source / 4` and is the single largest item at 65 KB.
zap's own note records the same shape: big.asm's 158 KB peak was 81% output
buffer.

### Measure it on the Agon, not the host

Host figures are roughly twice the truth and not by a constant. A pointer is
eight bytes there and three on the eZ80, so a symbol bucket is 16 bytes against
4 and a fixup 24 against 13. The host said 196 KB where the Agon says 167 KB.

Two things the plumbing needed, both worth not rediscovering:

* **The allocator renames have to be object-like.** `-Dmalloc(n)=...` is
  expanded inside stdlib.h's own declaration of malloc and the header stops
  compiling. That is also why attribution is a `z_site` string the caller sets
  rather than `__FILE__` and `__LINE__`.
* **The Makefile needed an EXTRA_CFLAGS hook.** Setting CFLAGS on the command
  line replaces what agondev's makefile put there, and the build fails in ways
  that look like the measurement simply not working -- an empty report rather
  than an error.

## The memory-degenerate case, and the ceiling it found

`gen_isa.sh memory` is the worst case for *memory*, which is a different file
from the worst case for time. isa_degenerate maximises the fixup list and uses
**less** memory than isa_real, because it has 468 distinct labels against 1,941
and the name arena and symbol blocks scale with the count rather than the
references.

A symbol costs eleven bytes of node plus its name, against the name plus two
bytes of source, so **short names are the expensive ones**:

    name length   source/label   table/label   ratio
              3              5            14   2.80x
              8             10            19   1.90x
             20             22            31   1.41x

    isa_memory (9,347 labels)   peak 238,140 bytes   45% of 512 KB
      source reader                   16,385
      output buffer                   65,538
      symbol buckets                   8,192
      symbol blocks                  107,065
      names and fixups                40,960

against isa_real's 167 KB. Symbol blocks alone are 45% of the peak.

### Where it stops fitting

One definition every *second* line -- 15,460 labels in 256 KB -- **does not
fit**: dzap runs out of memory at line 28,673 of 30,920, 93% of the way
through, wanting about 313 KB against the roughly 310 KB a 512 KB machine has
left after MOS and the program.

So the ceiling is near **14,000 labels for a 256 KB source**, and the committed
benchmark is one in three, which is 9,347 and runs. A benchmark that fails
measures nothing.

### Two things the tooling learned

* **An empty report must be an error.** memprofile.sh printed nothing when the
  guest failed, which made a build that did not take the flags, a guest out of
  memory, and an emulator that never started all look alike -- and it did that
  twice while this was being written. It now prints what the guest said.
* **A generated label can collide with the literal syntax.** The first names
  were a letter plus three base-36 digits and produced `a00h`, which is a
  hexadecimal literal with a trailing h: the reference reads it as 0xA00 and
  refuses it as a label. Leading letters now come from g..z.

  **dzap accepts `a00h:` where ez80asm rejects it**, which is a real divergence
  and is not fixed. The operand parser resolves that ambiguity toward the
  number; the definition path does not apply the rule at all. Small to fix --
  refuse a definition whose name would parse as a literal -- and worth a test
  when it is.
