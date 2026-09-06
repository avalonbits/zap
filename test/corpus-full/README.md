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

Still to do, in the order the original note set out: confirm ez80asm assembles
them, confirm zap agrees byte for byte, then pin a representative subset so
timings stay comparable across months.
