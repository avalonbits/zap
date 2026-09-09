# Baseline

Recorded on fab-agon-emulator 1.2.4 with `test/bench/bench.sh`. Figures are
each assembler's own `Done in` line. Every output was byte-identical between
the two.

| source | zap | ez80asm | ratio | |
|---|---|---|---|---|
| bbcbasic | 3.80s | 22.42s | **0.17x** | ez80asm `-m` |
| rokky | 0.52s | 2.50s | **0.21x** | |
| synth | 7.04s | 45.64s | **0.15x** | ez80asm `-m` |

Two changes moved these from 4.12 / 0.56 / 7.30: a comment stopped being
walked through an out-parameter, and the assembler's state moved to a fixed
address. Both are in .internal/performance-notes.md. The first is worth 3.4%
on bbcbasic and nothing on rokky, which is 6% comment by byte against BBC
BASIC's 28%; the second is worth about 4.5% everywhere.

The figures above are the default, which does not check whether a value fits
where it is written. `-w` asks for the check. Three builds, one source set:

| source | no warning code | default | `-w` |
|---|---|---|---|
| bbcbasic | 3.78s | 3.80s | 3.88s |
| rokky | 0.52s | 0.52s | 0.54s |
| synth | 6.94s | 7.04s | 7.06s |
| isa_real | 5.36s | 5.42s | 5.72s |
| isa_even | 5.46s | 5.52s | 5.80s |
| isa_degenerate | 5.18s | 5.22s | 5.28s |
| isa_memory | 5.56s | 5.60s | 5.60s |

Two numbers to take from that. **The check costs up to 6.7% and 2% on a real
program**, which is why it is a flag: the spread across the isa files says the
cost is proportional to how many operands are immediates, and isa_real and
isa_even are nothing but instructions with one operand each while isa_memory
addresses memory and pays nothing.

**Having the flag costs about 1% while it is off**, and that residual is a
call: the compiler makes `warn_imm` a real function, so every immediate calls
it to be told there is nothing to do. Hoisting the test to the call site
removes the call and is *slower* -- 5.44 on isa_real -- because it takes
`assemble_line`'s frame from 108 bytes to 111. That is written up in
.internal/performance-notes.md.

Lower is better. **The goal was 0.50x and it is met with room to spare** --
between four and six times faster than the reference rather than the two the
target asked for.

## What this replaces

The first table in this file, taken when the runner was fixed:

| source | zap | ez80asm | ratio |
|---|---|---|---|
| bbcbasic | 20.96s | 22.44s | 0.93x |
| rokky | 2.40s | 2.48s | 0.97x |
| synth | 40.78s | 45.60s | 0.89x |

So zap is **5.1x, 4.3x and 5.6x** faster than it was, on the same three
sources and the same rig.

**ez80asm's three figures are the check on that.** They were not supposed to
move and they did not: 22.44 to 22.42, 2.48 to 2.50, 45.60 to 45.64, all
within a run-to-run hundredth on a rig that has been rebuilt and a machine
that has been rebooted many times in between. A speedup measured against a
reference that had drifted would be worth nothing, and this is the evidence
that it has not.

## ez80asm gets -m only above 256 KiB of source

`-m` is ez80asm's minimum memory configuration. Without it, it sizes its
buffers for a desktop and never finishes on a 512 KB machine: bbcbasic sat on
"Pass 1..." indefinitely. rokky, a fifteenth the size, completed normally
either way, which is what made the failure look like a hang in the runner
rather than the assembler running out of room.

Passing it everywhere would be simpler and would not be fair. `-m` costs
ez80asm real time -- rokky is 2.50s without it and 2.70s with -- and nobody
reaches for it until they have to, so timing against a flag a user would not
have used makes zap look better than it is.

The threshold is on **the whole source the assembler reads**, includes and
all, because that is what drives the memory it needs. The size of the file
named on the command line would get it exactly backwards: bbcbasic's top-level
source is 554 bytes and its include tree is 386 KB.

    bbcbasic  386,345 bytes  -m
    rokky      25,171 bytes  no -m
    synth     471,286 bytes  -m

The runner prints `ez80asm -m` beside the sources that got it, so a reader
cannot mistake which comparison a row is.

## Regenerating

    make                      # zap.bin for the Agon
    test/bench/bench.sh       # all three
    test/bench/bench.sh rokky # just one

Both binaries are snapshotted when the run starts, so a `make` while a run is
in flight cannot change what is being measured half way through. Do not edit
`bench.sh` during a run either -- bash reads a script incrementally by offset,
so rewriting it under a running instance makes it execute garbage. That
happened once and ended a run with `unexpected EOF` after the table had
already printed.
