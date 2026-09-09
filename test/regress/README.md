# zap's own differential sources

The same shape as `test/corpus` and run in the same pass by `test/corpus.sh`,
but not the same provenance: `test/corpus` is the reference's test suite,
vendored, MIT licensed, and somebody else's. Nothing zap writes goes in there.

**What these are for.** A vendored corpus can only find a divergence somebody
else already wrote down. Every source in it was written to test ez80asm, so
the cases it covers are the cases its authors thought of -- and when zap
started reading the reference's *diagnostic table* instead, eight divergences
turned up that 507 sources had never touched. Three of them were wrong bytes
with nothing said. Those are here, so that a regression shows up in the runner
that has to stay green rather than in a survey somebody has to think to do
again.

Every file must either assemble identically in both or be refused by both.
That is the only thing the runner can check, and it is the thing that matters.

    folds/      operands that go into the opcode: bit numbers, interrupt
                modes, restart addresses -- valid, out of range, negative,
                and each of them again with the value still ahead of the
                instruction, which is where the fixup has to land on the
                opcode byte rather than after it
    addresses/  ORG and RELOCATE against 16 and 24 bits, in and out of ADL
                mode, including the two the reference does *not* check
    limits/     the longest line and the longest macro name, at both sides
                of the boundary, LF and CRLF
    values/     bytes that are warned about but still written -- truncation,
                a reservation's dropped initializer, an immediate wider than
                the machine -- and what DS, ALIGN and BLK each emit
    listing/    sources whose `.lst` is compared as well as their bytes

## What is deliberately not here

**The four differences zap keeps on purpose.** They are argued in
.internal/completeness.md and a file for any of them would fail by design: a
negative reservation (4 GB in the reference), a FILLBYTE that would change a
reservation already written, `@local - global` with both still ahead, and the
reference substituting a macro parameter inside a longer identifier.

**Anything that only differs in a message.** The runner compares bytes and
refusals. zap's diagnostics are its own words and always have been, so a
warning that zap prints and the reference does not -- or the other way round
-- cannot be seen here. test/run.sh compares those against the reference
directly, and that is where a warning's boundaries are pinned.

**Anything that depends on a command-line option.** Every source here is
assembled with the same flags: `-ez80` for zap, so that the two agree about
operator precedence, and nothing else. `-o`, `-b` and `-a` change the bytes
and are compared against the reference in test/run.sh.

## The listing group, and why it is only some of the sources

Three things a one-pass assembler cannot put in a listing the way a two-pass
one does, all written up in .internal/completeness.md:

* the reference widens the line-number column for the whole file when it
  lists a macro expansion, which it decides before it writes line 1;
* a macro body loses the indentation it was written with, because the body is
  stored from its first token;
* a reservation's fill is listed on a continuation row with the first row
  left empty, and an ALIGN's too, while an ORG's pad is listed inline. zap
  lists all three inline.

Sources in `listing/` avoid all three, so their `.lst` can be compared byte
for byte -- which is how it was found that zap wrote CRLF where the reference
writes LF, and how the fourth difference was found and then closed: a forward
reference was listed with the bytes as emitted rather than as patched, and
`list_forward.s` and `list_forward_local.s` are here to keep it closed.
Sources anywhere else in this tree are compared on bytes alone.
