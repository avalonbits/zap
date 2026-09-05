# Baseline

Recorded on fab-agon-emulator 1.2.4 with `test/bench/bench.sh`, at the commit
that fixed the runner. Figures are each assembler's own `Done in` line. Every
output was byte-identical between the two.

| source | zap | ez80asm | ratio | |
|---|---|---|---|---|
| bbcbasic | 20.96s | 22.44s | 0.93x | ez80asm `-m` |
| rokky | 2.40s | 2.48s | 0.97x | |
| synth | 40.78s | 45.60s | 0.89x | ez80asm `-m` |

Lower is better. The goal is **0.50x**: bbcbasic at 11.2s, rokky at 1.24s, synth
at 22.8s -- about a 45% cut across the board.

## ez80asm gets -m only above 256 KiB of source

`-m` is ez80asm's minimum memory configuration. Without it, it sizes its buffers
for a desktop and never finishes on a 512 KB machine: bbcbasic sat on
"Pass 1..." indefinitely. rokky, a fifteenth the size, completed normally either
way, which is what made the failure look like a hang in the runner rather than
the assembler running out of room.

Passing it everywhere would be simpler and would not be fair. `-m` costs
ez80asm real time -- rokky is 2.48s without it and 2.70s with -- and nobody
reaches for it until they have to, so timing against a flag a user would not
have used makes zap look better than it is. Charging it only where the source
actually needs it moved rokky's ratio from a flattering 0.87x to 0.97x.

The threshold is on **the whole source the assembler reads**, includes and all,
because that is what drives the memory it needs. The size of the file named on
the command line would get it exactly backwards: bbcbasic's top-level source is
554 bytes and its include tree is 400 KB.

    bbcbasic  408,119 bytes  -m
    rokky      60,861 bytes  no -m
    synth     471,286 bytes  -m

The runner prints `ez80asm -m` beside the sources that got it, so a reader
cannot mistake which comparison a row is.

## What these numbers replace

The previous benchmark, `big.asm`, existed only in a scratch directory and is
gone. `synth` is a different file and its timings do not continue that series;
the 57.88s figure quoted against big.asm has no successor here.

Two figures span both rigs and both reproduce: rokky at 2.40s exactly, and zap
on bbcbasic at 20.96s against the old rig's 20.96s. That is the only evidence
available that the two setups agree.

## Regenerating

    make                      # zap.bin for the Agon
    test/bench/bench.sh       # all three
    test/bench/bench.sh rokky # just one

Both binaries are snapshotted when the run starts, so a `make` while a run is in
flight cannot change what is being measured half way through. Do not edit
`bench.sh` during a run either -- bash reads a script incrementally by offset,
so rewriting it under a running instance makes it execute garbage. That happened
once and ended a run with `unexpected EOF` after the table had already printed.
