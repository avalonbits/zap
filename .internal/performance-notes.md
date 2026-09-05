# Where zap stands, and what is left

Notes to self, 2026-09-05. Everything here is measured, not estimated, unless
it says otherwise. Timings are on fab-agon-emulator 1.2.4 without `-u`, which
decouples the guest clock from guest work and makes every number meaningless.

## Where we are

| | zap | ez80asm |
|---|---|---|
| big.asm (473 KB, 99 KB out) | **59.02s** | 59.32s |
| BBC BASIC (386 KB tree, 20.9 KB out) | **23.22s** | — |
| rokky | 2.68s | — |
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

### 1. Lazy prescan — 6.8% to 11.0% of instructions, for 249 files in 250

The prescan exists to harvest `name: equ <expr>` before the real pass, so that a
directive which must size something immediately can see a symbol defined further
down. It costs 6.8% of instructions on big.asm and 11.0% on BBC BASIC, and it
runs on every file.

I disabled it and ran the whole corpus. **249 of 250 sources are unaffected**,
as are BBC BASIC, rokky and snes. Exactly one file needs it,
`Labels/EQU_order_in_defines.s`:

    before: equ 0x01
         db before, after1
         assume adl=before
        blkb before,after1
    after1: equ 0x01

So: assemble without the prescan, and only on that specific failure, re-init and
retry with it. The 1-in-250 case then pays one extra pass, which is exactly what
all 250 pay today.

**Audit this before building it.** The design is only safe if every directive
that needs a value immediately *errors* on an undefined symbol and never
silently substitutes 0. If one of them quietly uses 0, lazy prescanning
miscompiles silently instead of retrying, which is far worse than the cost it
saves. The corpus supports the safe reading — the single divergence was a clean
rejection and zero files assembled to different bytes — but that is evidence,
not proof. Read every sizing directive.

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
output is 81% of big.asm's peak — but **measure the seek first**: agondev builds
FatFS with `FF_USE_FASTSEEK 0`, so every seek is a linear walk of the FAT chain.
On a 100 KB output that may not be cheap, and if it is not, the whole idea dies
there. One microbenchmark on the emulator settles it before any of the work.

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
