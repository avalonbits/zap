# zap's own differential sources

These work like `test/corpus` and `test/corpus.sh` runs them in the same pass,
but they're ours. `test/corpus` is ez80asm's own test suite, vendored under
its MIT licence, and nothing zap adds goes in there.

A borrowed test suite only covers the cases its authors thought of. When I
went through ez80asm's table of error messages instead, I found eight
differences that none of its 507 sources exercised, three of them silently
wrong bytes. They live here now, along with later finds, so a regression shows
up in the normal test run.

Each file must either assemble to identical bytes in both assemblers or be
rejected by both. That's all the runner checks.

    folds/      operands encoded into the opcode (bit numbers, interrupt
                modes, restart addresses): valid, out of range, negative,
                and forward-referenced
    addresses/  ORG and RELOCATE against 16 and 24 bits, in and out of ADL
                mode, including two cases ez80asm doesn't check
    limits/     the longest line and longest macro name, either side of the
                limit, with LF and CRLF
    values/     values that warn but still assemble (truncation, a DS
                initializer, an immediate too wide), and what DS, ALIGN and
                BLK emit around FILLBYTE
    scopes/     label arithmetic that mixes local and global labels across
                a scope boundary
    listing/    sources whose `.lst` is compared as well as their bytes

## What isn't here

**The three intentional differences.** They're explained in section 13 of
docs/DESIGN.md, and a test for any of them would fail by design: a negative
`DS` (ez80asm writes 4 GB), output past the 24-bit address range, and ez80asm
substituting a macro parameter inside a longer name (`db max` with a
parameter `x` becomes `db ma1`). All three still hold in 2.3.

**Differences in messages only.** The runner compares bytes and refusals, not
text. Warnings are compared against ez80asm in test/run.sh instead.

**Anything that depends on command-line options.** Every file here is
assembled with `-ez80` for zap (so operator precedence matches) and nothing
else. `-o`, `-b` and `-a` are tested in test/run.sh.

## Why only some sources compare listings

A single-pass assembler can't reproduce two things in ez80asm's listings (see
section 12 of docs/DESIGN.md):

- a macro body loses its original indentation, because zap stores the body
  from its first token;
- ez80asm lists a reservation's fill bytes on an extra row, and zap doesn't.
  `ORG` padding is listed inline by both and matches.

The files in `listing/` avoid both, so their listings can be compared byte for
byte. That's how we caught zap writing CRLF where ez80asm writes LF, and how we
found that forward references were listed with their placeholder bytes;
`list_forward.s` and `list_forward_local.s` keep that fixed. Everywhere else
only the binary output is compared.
