# The benchmark set

Six fixed sources, so a number taken today can be compared with one taken in
six months. Run them with `test/bench/bench.sh`.

| | what it is | why |
|---|---|---|
| `bbcbasic` | BBC BASIC for Agon, 20 files, ~386 KB | The realistic one: deep includes, macros, and forward references across most of the output |
| `rokky` | a smaller real program, ~25 KB | A different shape, and quick to iterate on |
| `synth` | 471 KB from `gen_synth.sh` | Plain instructions with no labels, macros or includes, to isolate parsing and encoding |
| `isa_even` | 256 KB from `gen_isa.sh even` | Every form zap assembles, each equally often |
| `isa_real` | 256 KB from `gen_isa.sh real` | Every form, weighted by how often it appears in BBC BASIC and Rokky |
| `isa_include` | ten sources and three binaries from `gen_isa.sh include` | Nested includes, four deep. Not comparable with the others, since opening thirteen files on the emulated card is extra work |

The first two come from `test/corpus`. The rest are generated deterministically
(line *i* depends only on *i*) so they don't need to be committed but never
drift. If you change `gen_synth.sh`, every earlier timing becomes incomparable,
so add a new generator instead.

## How timings are taken

The number is the one each assembler prints itself (`Done in X.XX seconds`).
Wall-clock time on the host is useless here because it includes emulator
startup, MOS boot and SD card I/O.

The emulator runs without `-u`. Unthrottled, the guest's clock no longer tracks
the work it does, and the numbers stop meaning anything.

A source that prints no time shows as `-`, never 0. No number means something
crashed or the output was lost.

## Why not just profile on the host

The host and the Agon disagree, often a lot and in both directions. Under
callgrind zap executes 0.71x as many instructions as ez80asm, but on the Agon
it took 0.98x the time. The host's libc is vectorised and the Agon's is a byte
loop, which flatters ez80asm's `strcasecmp`/`strchr` calls; chasing pointers is
cheap on x86 and expensive on a cacheless eZ80, which flatters zap. It goes the
other way too: one change saved 4.0% of host instructions and 1.8% on the
Agon.

Use callgrind to find where the work is, and this to check whether removing it
helped.

## Current numbers

`RATIO` is zap's time divided by ez80asm's, so lower is better. Against
ez80asm 2.2 the goal was 0.50x and zap got to 0.16x-0.21x. Against 2.3, which
is what's vendored now, it's 0.68x on bbcbasic, 0.64x on rokky and 0.56x on
synth. Details and history are in `BASELINE.md`.

## isa_even and isa_real

The other sources couldn't tell whether a change to instruction handling was an
improvement: `synth` and the older hand-written file cover 31 of the 114
mnemonics and 40 of the 322 table rows, with no `call`, `jp` or `djnz` at all.

`gen_isa.sh even` uses every one of the 1,083 forms equally often.
`gen_isa.sh real` weights them by how often each mnemonic appears in BBC BASIC
and Rokky (10,440 instructions), while still including every form at least
once. `even` shows what the whole instruction set costs; `real` shows what
typical code costs. If a change helps one and not the other, find out why
before keeping it. The first time these ran, an operand-parsing change that
looked 1.5% faster on the old file was 0.9% and 1.2% slower on these two.

Neither contains relative jumps, because without labels there's no way to keep
a target in range as the file grows. `jr` and `djnz` are about 10% of real
instructions, so `real` is optimistic by about that much; `test/cases/relative.s`
covers them for correctness.

## Measuring a single change

`test/bench/time-one.sh` times one binary on one source. To compare two
variants, build both, keep both, and run them against the same input:

    test/bench/time-one.sh bin/zap.bin isa_real.s
    test/bench/time-one.sh bin/zap.bin \
        test/corpus/Z_PRG_Agon-bbc-basic-v/tests bbcbasicvez.s

The second form stages a whole directory and names the entry file.

Always include bbcbasic when deciding whether something is faster. The
generated files were the right tool while finishing the assembler, since
they cover every form, but they don't look like real code:

| | bbcbasic | isa_real |
|---|---|---|
| source | 386 KB, 20 files | 256 KB, one file |
| lines | 14,758 | 21,494 |
| comment-only lines | 3,346 (23%) | 0 |
| lines with a trailing comment | 2,224 | 0 |
| cost | 197 cycles per byte | 405 |

Nearly a quarter of a real program is comments, which are cheap to skip. A
change that made comment scanning twice as slow would hurt bbcbasic and not
show up on isa_real at all, and an operand-parsing win will always look bigger
on isa_real than on real code. When the two disagree, work out why before
taking the change.

rokky (25 KB, about half a second) is a useful third: quick, and a different
shape again.
