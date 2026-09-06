# How the numbers in these notes were taken

Everything quoted in `dzap-to-zap.md` comes from the same three moves. They are
written down because the conclusions are worthless without them, and because
each one exists to stop a specific way of being wrong that has already happened
here.

## 1. One change, one measurement, on the Agon

`test/bench/time-one.sh <binary> <source.s>` times one binary against one
source and prints the seconds the assembler itself reported.

Build both variants, keep both, run them one after another. Never rebuild
between halves of a comparison, and never run two emulators at once -- they
share the host's CPU and both numbers drift.

Bundling has produced the wrong conclusion three times: a 1.3% regression
hidden inside a 22% win, a class table carrying a mask change that was actually
neutral, and an operand fast path that was the opposite of what it looked like.
Each change gets its own number or it does not get a claim.

**The host cannot substitute.** zap retires 0.71x ez80asm's instructions on the
host and takes 0.98x its time on an Agon. The largest single win in this work
(`uint24_t` register masks) is *exactly zero* instructions on the host, because
`uint24_t` is a typedef for `uint32_t` under the test stubs. Use callgrind to
find candidates, the Agon to size them.

## 2. Isolate a shape

`test/bench/gen_shape.sh <nop|reg|imm8|imm24|cb|disp>` writes 30,000 lines of
one instruction.

A mixed source only ever gives an average. The difference between two
single-shape files is the difference between those two instructions and nothing
else, which is how the row-scanning cost was found at all: `disp` reaches the
forty-third of `ld`'s 57 rows and nothing else in the set reaches past the
fifth.

`nop` is the important one. It is the cheapest instruction there is, so what it
costs is the floor every other instruction is built on, and it is what a new
feature has to be priced against.

Watch the direction of the two units. Cycles per *byte* runs backwards to
cycles per instruction -- `ld hl, 0x123456` is among the dearest instructions
and the cheapest per byte, because a long instruction spreads the fixed cost
over more source. Only the mixed file gives an honest per-byte figure.

## 3. Decompose by truncation

To find *where* a shape's time goes, build variants of `assemble_line` that
stop after each stage and measure each. The deltas are the stages:

    read the line and scan to the newline
    + classify the mnemonic run
    + mnemonic_of
    + both parse_operand calls
    + match_row
    + emit_row

The same trick nests: to divide `parse_operand` up, cut it at chosen points
while `assemble_line` still stops after the operands.

Two rules, both learned the hard way:

- **Sink every intermediate into a `volatile`.** Otherwise the compiler deletes
  the work whose result is unused and the stage measures nothing.
- **Never leave a variant in a state the rest of the program will read.** A
  probe that removed the operand zeroing left `match_row` reading uninitialised
  memory; the guest wandered off and sat for 469 seconds before it was killed.
  Truncate *downstream* -- stop `assemble_line` before the value is consumed --
  or measure by *adding* redundant work to a correct program rather than
  removing required work from it.

## What resolution to expect

Three interleaved repeats of two binaries on the same source:

    8.24  8.26  8.24
    8.28  8.28  8.28

**The emulator is deterministic to about 0.25%.** Half a percent is the
smallest honest claim from a single run, and a consistent 0.4% is not noise --
that difference is a real regression, and was treated as one.

## When a run seems slow

It has probably failed. A failed run does not end: the assembler returns
non-zero, MOS abandons `autoexec.txt` before reaching `emulator_exit_success`,
and the emulator sits at a prompt until the timeout, which looks exactly like a
long assembly from outside.

Both runners now watch the console and kill the run as soon as the guest says
it failed, printing what it said. If a run still seems slow, read the capture
before theorising -- once, twenty minutes went into waiting for an answer that
had been sitting in it for thirty seconds.

`test/bench/selftest.sh` tests that detection without an emulator.
