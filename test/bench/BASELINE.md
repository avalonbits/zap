# Baseline

Recorded on fab-agon-emulator 1.2.4 with `test/bench/bench.sh`, at the commit
that fixed the runner. Figures are each assembler's own `Done in` line. Every
output was byte-identical between the two.

| source | zap | ez80asm | ratio |
|---|---|---|---|
| bbcbasic | 20.94s | 22.42s | 0.93x |
| rokky | 2.40s | 2.70s | 0.89x |
| synth | 40.78s | 45.64s | 0.89x |

Lower is better. The goal is **0.50x**, which means bbcbasic at about 11.2s and
synth at about 22.8s.

## ez80asm is run with -m, on every source

`-m` is ez80asm's minimum memory configuration. Without it, it sizes its buffers
for a desktop and never finishes on a 512 KB machine: bbcbasic sat on
"Pass 1..." indefinitely. rokky, a fifteenth the size, completed normally either
way, which is what made the failure look like a hang in the runner rather than
the assembler running out of room.

It is used for **all** sources, not only the ones that need it, because a
benchmark whose flags vary with the input is not comparing one thing. The cost
of that consistency is visible: rokky's ez80asm figure is 2.70s with `-m` and
2.50s without, so the ratio on small sources flatters zap by about 8% relative
to an ez80asm run the way a desktop user would run it. On the Agon, `-m` is how
ez80asm is actually used for anything large, so it is the honest comparison --
but the 2.50s is worth remembering before quoting 0.89x anywhere it might be
read as a general claim.

## What these numbers replace

The previous benchmark, `big.asm`, existed only in a scratch directory and is
gone. `synth` is a different file and its timings do not continue that series;
the 57.88s figure quoted against big.asm has no successor here.

`rokky` at 2.40s is the one figure that spans both rigs, and it matches to the
hundredth, which is the only evidence available that the old and new setups
agree. zap's bbcbasic figure also reproduces: 20.94s here against 20.96s from
the old rig.

## Regenerating

    make                      # zap.bin for the Agon
    test/bench/bench.sh       # all three
    test/bench/bench.sh rokky # just one

Both binaries are snapshotted when the run starts, so a `make` while a run is in
flight cannot change what is being measured half way through. Do not edit
`bench.sh` during a run either -- bash reads a script incrementally by offset,
so rewriting it under a running instance makes it execute garbage. That happened
once and ended a run with `unexpected EOF` after the table had already printed.
