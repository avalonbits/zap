# Baseline

All figures are from fab-agon-emulator 1.2.4 via `test/bench/bench.sh`, using
the time each assembler reports itself. Both assemblers produced
byte-identical output in every run listed here.

## Against ez80asm 2.3 (current)

ez80asm 2.3 came out on 2026-09-13. It switched to a one-pass design with
fixups and a memory/file window, and adopted several of zap's optimizations
(its release notes say so). It's the vendored reference now, measured with the
v2.3 Agon release binary (sha256 `c3aeba4c...`), on 2026-09-21:

| source | zap | ez80asm 2.3 | ratio | |
|---|---|---|---|---|
| bbcbasic | 3.98s | 5.86s | 0.68x | 2.3 with `-m` |
| rokky | 0.54s | 0.84s | 0.64x | |
| synth | 7.26s | 13.00s | 0.56x | 2.3 with `-m` |
| 256 KB of output (p2-256k) | 0.72s | 6.36s | 0.11x | |
| 1 MiB of output (p2-1m) | 2.86s | 25.32s | 0.11x | |

Against 2.2 the ratios on ordinary source were 0.16x-0.21x, so the gap has gone
from four-to-six times to about one and a half. synth was 7.32s before zap
adopted 2.3's FILLBYTE behaviour and 7.26s after; that's within the normal
run-to-run noise.

2.3 still needs `-m` for the large sources. I checked by turning the threshold
off: without it, 2.3 produced no output for bbcbasic or synth.

Read the two output-size rows as ratios, not absolute times. The emulator
doesn't charge anything for writing to the SD card. A real Agon writes at about
68 KB/s once the output no longer fits in one window, and there the 1 MiB file
takes 15.2s.

## History against ez80asm 2.2

These were taken with the v2.2 binaries (hashes in `test/ref/README.md`).

### After the output window, 2026-09-15

Streaming the output through a window and sweeping the fixup list both added
work to every assembly, so the set was re-run on the same setup:

| source | zap | ez80asm 2.2 | ratio | |
|---|---|---|---|---|
| bbcbasic | 3.92s | 22.44s | 0.17x | ez80asm `-m` |
| rokky | 0.52s | 2.50s | 0.21x | |
| synth | 7.18s | 45.64s | 0.16x | ez80asm `-m` |
| isa_even | 5.68s | 33.54s | 0.17x | |
| isa_real | 5.56s | 33.14s | 0.17x | |
| isa_include | 5.74s | 36.02s | 0.16x | ez80asm `-m` |

No measurable change: the differences from the table below are within normal
noise, and ez80asm's own times barely moved (22.44 vs 22.42, 2.50 vs 2.50,
45.64 vs 45.64), which shows the setup itself didn't drift. None of these
sources produces enough output to fill a window, so the cost of streaming on
large outputs has to be measured on hardware (`test/hwkit.sh`).

### The main 2.2 baseline

| source | zap | ez80asm 2.2 | ratio | |
|---|---|---|---|---|
| bbcbasic | 3.86s | 22.42s | 0.17x | ez80asm `-m` |
| rokky | 0.54s | 2.50s | 0.22x | |
| synth | 7.16s | 45.64s | 0.16x | ez80asm `-m` |

These came down from 4.12 / 0.56 / 7.30 thanks to two changes described in the
optimization guide: comments stopped being scanned through an out-parameter
(3.4% on bbcbasic, nothing on rokky, which has far fewer comments), and the
assembler state moved to a fixed address (about 4.5% everywhere). The goal was
0.50x; zap ended up four to six times faster than 2.2.

### The cost of the truncation warning

The figures above are without `-w`. Adding eight validation checks that
ez80asm had and zap didn't (the most expensive being the 256-character line
limit) cost 1.5-1.9% across the set. Three builds of the same sources:

| source | no warning code | default | `-w` |
|---|---|---|---|
| bbcbasic | 3.78s | 3.80s | 3.88s |
| rokky | 0.52s | 0.52s | 0.54s |
| synth | 6.94s | 7.04s | 7.06s |
| isa_real | 5.36s | 5.42s | 5.72s |
| isa_even | 5.46s | 5.52s | 5.80s |
| isa_degenerate | 5.18s | 5.22s | 5.28s |
| isa_memory | 5.56s | 5.60s | 5.60s |

The check costs up to 6.7%, and 2% on a real program, which is why it's
behind a flag. The cost tracks how many operands are immediates: isa_real and
isa_even are full of them, while isa_memory pays nothing.

Just having the flag costs about 1% when it's off, because the compiler turns
`warn_imm` into a real function that every immediate calls. Moving the test to
the call site removes the call but is slower (5.44s on isa_real), because it
grows `assemble_line`'s frame from 108 to 111 bytes. The optimization guide has
the details.

### The whole corpus

`test/bench/corpus-target.sh` runs every source both assemblers accept through
both of them on the emulated Agon. Per-source times are in
`test/bench/corpus-target-times.txt`. Small sources are run twelve times and
summed, since the clock only has hundredths and a single zap run can be 0.02s.

    161 sources both accept, of 558 in test/corpus and test/regress
    135 of them take long enough to measure on both sides

    geometric mean speedup   3.20x
    median 3.03x, quartiles 2.03x and 4.03x, range 0.5x to 36.7x
    summed: zap 7.67s against ez80asm 44.41s -- 5.79x

    under 1x   4    all four take 3 to 10 ms, one tick of the clock
    1x to 2x  10
    2x to 4x  76
    4x to 8x  38
    8x and up  7    large_include 36.7x, allowed16bitlabel_adl0 13.7x,
                    opcodes_l 13.5x

| source | zap | ez80asm 2.2 | speedup |
|---|---|---|---|
| bbcbasicvez | 3.86s | 22.42s | 5.8x |
| rokky | 0.54s | 2.50s | 4.6x |
| allowed16bitlabel_adl0 | 0.51s | 6.97s | 13.7x |
| compound_binarytest | 0.38s | 1.62s | 4.3x |
| z80_undocumented | 0.16s | 1.10s | 6.9x |
| large_include | 0.03s | 1.10s | 36.7x |

The geometric mean (3.20x) and the summed ratio (5.79x) differ because the
mean weights a twenty-line test the same as BBC BASIC. Small sources spend most
of their time on things neither assembler can avoid (opening files, reading the
first buffer, writing the output), so the two are closest there. The bigger
the program, the more time goes into the optimized loop and the bigger the
gap. The same measurement on the host gives 1.36x; section 5 of the
optimization guide explains why.

### Where it started

The first table, from when the benchmark runner was set up:

| source | zap | ez80asm 2.2 | ratio |
|---|---|---|---|
| bbcbasic | 20.96s | 22.44s | 0.93x |
| rokky | 2.40s | 2.48s | 0.97x |
| synth | 40.78s | 45.60s | 0.89x |

So zap got 5.1x, 4.3x and 5.6x faster on the same sources and setup.
ez80asm's numbers are the sanity check: they stayed within a hundredth of a
second (22.44 to 22.42, 2.48 to 2.50, 45.60 to 45.64) across many rebuilds and
reboots, so the improvement isn't the setup drifting.

## When ez80asm gets `-m`

`-m` is ez80asm's low-memory mode. Without it, it sizes its buffers for a
desktop and never finishes large sources on a 512 KB machine; bbcbasic just
sat at "Pass 1...". But `-m` also costs ez80asm time (rokky: 2.50s without,
2.70s with), and nobody uses it unless they have to, so passing it everywhere
would make zap look better than it is.

The runner passes `-m` when the whole source, includes and all, is over
256 KiB. Using the size of the top-level file would get it backwards:
bbcbasic's main file is 554 bytes but its include tree is 386 KB.

    bbcbasic     386,345 bytes  -m
    rokky         25,171 bytes  no -m
    synth        471,286 bytes  -m
    isa_even     262,113 bytes  no -m
    isa_real     262,114 bytes  no -m
    isa_include  265,849 bytes  -m   (whole tree)

The two isa files are just 30 bytes under the threshold, by luck:
`gen_isa.sh` stops at the line that would pass 256 KB. If a generator change
ever pushes them over, note it in the table, since it changes the comparison.
The runner prints `ez80asm -m` next to any source that got it.

## Regenerating

    make                      # zap.bin for the Agon
    test/bench/bench.sh       # all six
    test/bench/bench.sh rokky # just one

Both binaries are copied when the run starts, so running `make` mid-run won't
change what's being measured. Don't edit `bench.sh` during a run, though: bash
reads scripts incrementally, and changing one while it runs makes it execute
garbage. That has happened once, ending a run with `unexpected EOF`.
