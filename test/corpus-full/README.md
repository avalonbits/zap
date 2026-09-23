# The full corpus

`harvest.sh` clones every Agon project linked from
[sabotrax/agon-software](https://github.com/sabotrax/agon-software) into a
directory outside this repository (`~/agon-corpus` by default).

    test/corpus-full/harvest.sh [dir]

None of it is vendored. These are 89 projects under their authors' own
licences, and copying them in just to have a benchmark isn't right. What lives
here are the scripts and the numbers they produced, so anyone can reproduce
them. If a program is ever worth committing as a benchmark, that's a decision
for one project at a time, with its licence, the way `test/corpus` vendors
ez80asm's MIT-licensed suite.

The survey below was done in early September 2026, before zap replaced its
original implementation, so a few details (like the 26-character label limit)
describe zap as it was then.

## What it contains

Harvested 2026-09-06: 89 repositories linked, 87 cloned, two unreachable
(`envenomator/Agon`, `envenomator/console8-recover`).

z88dk is excluded from every figure here. It's 16,500 of the 17,496 assembly
files and 93% of the lines, and it's a Z80 toolkit for dozens of machines, not
Agon code. That leaves:

    86 repositories, 996 assembly files, 181,753 lines

compared with the 14,757 lines of BBC BASIC every earlier measurement used.
919 of the 996 files use something eZ80-only (`.assume adl`, `lea`, `mlt`,
`ld a, mb`, an ADL suffix), so this really is eZ80 code.

ez80asm's own test suite (`envenomator/agon-ez80asm`) is also left out of the
label numbers: it's already in `test/corpus`, and its error tests are
synthetic (its 65-character label only exists to be rejected).

## Label lengths in 25 real programs

14,063 definitions:

    min 1   median 7   mean 8.5   p95 17   p99 22   max 38

    longer than 16:  872  (6.20%)
    longer than 20:  221  (1.57%)
    longer than 26:   39  (0.28%)
    longer than 32:    7  (0.05%)

At the time zap limited labels to 26 characters, and the two programs we'd
looked at suggested that was enough. It wasn't: 39 real labels are longer, up
to 38 characters, and they're ordinary names like
`VDU_BufferBitmapExpandMappingBufferBit` in AgonConsole8's VDU code. zap now
allows 64, the same as ez80asm.

## Largest sources

The biggest single file in each project, which is where benchmark candidates
come from:

    99,523  nihirash-Agon-CPM2.2/sources/cpm.asm
    84,612  breakintoprogram-agon-bbc-basic-adl/exec.asm
    73,248  breakintoprogram-agon-bbc-basic/main.asm
    72,970  sijnstra-agon-projects/TinyBASIC/TinyBASIC.asm
    64,464  AgonConsole8-agon-mos/src/mos_api.asm
    61,123  nihirash-ZINC/apps/3rd-party/kermit/cpspk1.asm
    53,235  lennart-benschop-agon-utilities/nano.asm
    45,654  rickshoe2-AgonLight-Assembly-Programming/eZapple.asm

## What ez80asm makes of it

`assemble.sh` runs ez80asm over every entry point, meaning a file nothing else
in its project includes, assembled from its own directory. Most files are
includes and fragments that were never meant to assemble alone.

    223 entry points, 32 assemble (14%)

That's not a problem with the corpus or with ez80asm. Most Agon assembly out
there is written for Zilog ZDS II, which is a different dialect:

- `XREF`, `SEGMENT` and `DEFINE` are ZDS directives ez80asm doesn't have.
  They're what stop `breakintoprogram/agon-bbc-basic-adl` and
  `AgonConsole8/agon-mos` (the BBC BASIC in `test/corpus` is a port).
- ez80asm requires a colon: `FOO: EQU 5` works and `FOO EQU 5`, the ZDS style,
  doesn't. 15 of the 36 projects that define equates use the ZDS style.
- Some projects include Zilog headers (`ez80f92.inc`) they don't ship.

zap's goal is to agree with ez80asm, not to accept more than it does, so the
ZDS half of the corpus is out of scope. (The label statistics above still
count all 25 programs, since naming style doesn't depend on the target
assembler.)

## Sources that assemble

| output | source |
|---|---|
| 31,520 | `nihirash-Agon-rokky/src/rokky.asm` (already in `test/corpus`) |
| 30,464 | `sijnstra-agon-projects/TinyBASIC/TinyBASIC.asm` |
| 6,560 | `lennart-benschop-agon-utilities/nano.asm` |
| 4,317 | `jblang-z80demos/plasma.asm` |
| 2,939 | `tomm-toms-agon-experiments/tetris/main.asm` |
| 2,556 | `sijnstra-agon-projects/calc24/calc24.asm` |

TinyBASIC is the one worth adding as a benchmark: a real single-file program,
1,878 lines and 227 labels, producing 30 KB, similar in size to rokky but
unrelated to it. Everything bigger in the corpus is ZDS.

### The ZDS projects

Supporting ZDS syntax in zap would mean carrying a second dialect forever to
read files a converter could rewrite once. That converter lives in a separate
repository (`~/code/zds2ez80`).

The mechanical parts convert fine: dropping XREF, XDEF, SEGMENT and DEFINE,
adding colons, rewriting macro headers. That gets BBC BASIC from failing on
line 10 of its first include to failing on ZDS's `$$` local labels, of which
the corpus has 293. Two problems remain: those labels, and the fact that
ez80asm has no linker, so a project has to become a single translation unit,
and the link order in a `.zdsproj` isn't a valid include order. Only 6 of the
15 ZDS projects ship a `.zdsproj` anyway.

## Does zap agree?

`verify.sh` runs zap over the 32 entry points ez80asm accepts and compares the
output. It found three disagreements, all since fixed:

- An `include` on the last line of a file with no trailing newline. The lexer
  switched to the included file before the end-of-line check ran, so the check
  read the wrong file's first token. ZINC's `zinc-setup.asm` ends exactly like
  that, and the error pointed at line 109 of a fourteen-line file.
- A second macro expansion reused the first one's scope, so a forward `@label`
  in every later expansion resolved to the earlier one's. In a `puts` macro
  that jumps over an inline string, the jump went backwards: one wrong byte in
  2,939.
- Macro arguments lost the spaces inside quoted strings, so a program printed
  `Runningtests`.

Now all 32 agree. None of the three could be reached from the vendored test
suite, and the first two produced plausible-looking bytes rather than errors.

Still to do: pin a benchmark subset so timings stay comparable over time.
