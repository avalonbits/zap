# The full corpus

`harvest.sh` clones every Agon project linked from
[sabotrax/agon-software](https://github.com/sabotrax/agon-software) into a
directory **outside this repository**, `~/agon-corpus` by default.

**Nothing is vendored, deliberately.** Eighty-nine projects under whatever
licences their authors chose is not something to copy into zap as a side effect
of wanting a benchmark. What lives here is the script and the figures derived
from what it fetches, so any claim made from the corpus can be reproduced
without the corpus being in the tree. If a program is ever wanted as a
committed benchmark, that is a decision to take one project at a time, with its
licence, the way `test/corpus` vendors ez80asm's suite under its MIT licence.

    test/corpus-full/harvest.sh [dir]

## What it contains

Harvested 2026-09-06: 89 repositories linked, **87 cloned**, two unreachable
(`envenomator/Agon`, `envenomator/console8-recover`).

**z88dk is excluded from every figure below.** It is 16,500 of the 17,496
assembly files and 2.33M of the 2.51M lines -- 93% of the corpus -- and it is a
Z80 cross-toolkit for dozens of machines, not Agon code. It has 51 Agon-named
files and a small `arch/ez80` tree, which could be folded in later; the rest
would drown everything else.

What is left is the Agon sample:

    86 repositories, 996 assembly files, 181,753 lines

against the 14,757 lines of BBC BASIC that every measurement before this used.
919 of the 996 files carry an eZ80-only construct (`.assume adl`, `lea`, `mlt`,
`ld a, mb`, an ADL suffix), so it is genuinely eZ80 and not Z80.

The reference's own test suite (`envenomator/agon-ez80asm`) is also excluded
from the label figures: it is already vendored in `test/corpus`, and its error
tests are synthetic -- the 65-character label in it comes from
`Errors_labels/tests/locallabel_illegal_maxlength.s`, which exists to be
rejected.

## Labels, over 25 real programs

14,063 definitions:

    min 1   median 7   mean 8.5   p95 17   p99 22   max 38

    longer than 16:  872  (6.20%)
    longer than 20:  221  (1.57%)
    longer than 26:   39  (0.28%)
    longer than 32:    7  (0.05%)

**zap's 26-character limit is reached by real code**, which the two-program
sample said it was not: 39 labels exceed it, the longest being 38, and they are
ordinary names rather than pathological ones -- `VDU_BufferBitmapExpandMappingBufferBit`
and its neighbours in AgonConsole8's VDU code. ez80asm allows 64.

## Benchmark candidates

Largest single source per project, which is where a fixed benchmark set should
be drawn from:

    99,523  nihirash-Agon-CPM2.2/sources/cpm.asm
    84,612  breakintoprogram-agon-bbc-basic-adl/exec.asm
    73,248  breakintoprogram-agon-bbc-basic/main.asm
    72,970  sijnstra-agon-projects/TinyBASIC/TinyBASIC.asm
    64,464  AgonConsole8-agon-mos/src/mos_api.asm
    61,123  nihirash-ZINC/apps/3rd-party/kermit/cpspk1.asm
    53,235  lennart-benschop-agon-utilities/nano.asm
    45,654  rickshoe2-AgonLight-Assembly-Programming/eZapple.asm

## Step one: what ez80asm makes of it

`assemble.sh` runs the reference over every **entry point** -- a file nothing
else in its project includes, assembled from its own directory. Most of the
corpus is includes and fragments that were never meant to assemble alone, and
sweeping every file would produce a pile of failures that say nothing.

    223 entry points, 32 assemble (14%)

That number is not a defect in the corpus, in ez80asm, or in the sweep. **Most
Agon assembly in the wild is written for Zilog ZDS II, not for ez80asm**, and
the two are different dialects:

* `XREF`, `SEGMENT`, `DEFINE` are ZDS directives ez80asm does not have. They are
  what stops `breakintoprogram/agon-bbc-basic-adl` and `AgonConsole8/agon-mos`
  -- the copy of BBC BASIC in `test/corpus` is a port, not the upstream source.
* **ez80asm requires a colon**: `FOO: EQU 5` assembles and `FOO EQU 5` does not,
  which is ZDS's spelling and appears in 15 of the 36 projects that define
  equates at all. 21 use the colon form.
* Some projects include Zilog headers (`ez80f92.inc`) they do not ship.

Anything the reference rejects is out of scope, by the rule the original note
set: zap is not trying to be better than ez80asm, it is trying to agree with
it. So the ZDS half of the corpus is not a target, and saying so is the useful
result of this step.

**This does not affect the label figures above.** Naming style does not depend
on which assembler a file targets, so all 25 programs count for that.

## Benchmark candidates that actually assemble

| output | source |
|---|---|
| 31,520 | `nihirash-Agon-rokky/src/rokky.asm` (already in `test/corpus`) |
| **30,464** | **`sijnstra-agon-projects/TinyBASIC/TinyBASIC.asm`** |
| 6,560 | `lennart-benschop-agon-utilities/nano.asm` |
| 4,317 | `jblang-z80demos/plasma.asm` |
| 2,939 | `tomm-toms-agon-experiments/tetris/main.asm` |
| 2,556 | `sijnstra-agon-projects/calc24/calc24.asm` |

**TinyBASIC is the one worth adding**: a real program, single file, 1,878 lines
and 227 labels, assembling to 30 KB -- comparable in output to rokky and
independent of it. Everything larger in the corpus is ZDS.

### The ZDS half is a separate problem, and a separate repository

Supporting the ZDS dialect in zap would put a second syntax in the assembler
for the whole of its life, to read files a converter can rewrite once. The
converter lives in `~/code/zds2ez80` with its own briefing; what it needs is in
there rather than here.

Where it stands: the mechanical differences convert -- XREF, XDEF, SEGMENT and
DEFINE dropped, colons added to labels and equates, macro headers rewritten --
and BBC BASIC moves from failing on line 10 of its first include to failing on
ZDS `$$` local labels, of which the corpus has 293. Two problems are open: those
labels, and the fact that ez80asm has no linker, so a project must become one
translation unit and the link order the `.zdsproj` records is not a valid
include order.

Only 6 of the 15 ZDS projects ship a `.zdsproj` at all.

Still to do here: confirm zap agrees byte for byte on the 32 that assemble --
expect failures, that is the point -- and then pin a subset so timings stay
comparable across months.
