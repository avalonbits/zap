# Classifying identifiers by where they are, not against everything

Design note, 2026-09-05. Written before any code, because the last attempt at
restructuring the lexer failed and was abandoned, and the reason it failed is
worth not repeating.

## What was measured

BBC BASIC, 386 KB of source, 32,357 hash lookups in total:

    lexer classification   26,400   81.6%
    symbol table            5,776   17.9%
    is_mnemonic probe         181    0.6%

Of the 26,400 classification lookups, by what the identifier turned out to be:

    INSTRUCTION   9,875   37.4%
    REGISTER      7,699   29.2%
    DIRECTIVE     1,650    6.2%
    FLAG          1,045    4.0%
    NONE (a name) 6,131   23.2%

**76.8% of classification lookups resolve to a reserved word**, and all four
kinds are determined by position. A mnemonic or directive can only begin a
statement. A register or flag can only appear inside an operand. That is 20,269
lookups -- 62.6% of all hashing in the program -- spent discovering a category
the parser already knew when it asked.

## Why the obvious cheap fix does not work here

The macro reject in `mt_find` worked because macros are few and their first
characters are a small set, so one indexed load answered almost every call. The
equivalent here would be a cheap test that rejects a user name before it is
hashed against the reserved vocabulary.

There is not one. Measured on the same source:

  - The longest reserved word actually matched is **7 characters**.
  - Only **9.9%** of user names are longer than that.

So a length bound rejects one name in ten. A first-character table is worse:
reserved words start with most letters of the alphabet. The reserved vocabulary
is too dense in the space of short identifiers for a filter to separate it.

This is worth recording because it is the option I expected to recommend, and
the measurement said no.

## The constraint that shapes everything

zap must agree with ez80asm byte for byte, and ez80asm lets a label be named
after a mnemonic. `expr.c` accepts `NAME`, `INSTRUCTION` and `DIRECTIVE` as
symbol references for exactly this reason, and the corpus depends on it --
`FFOBJID.fs` against `rst.lil` is already called out in the lexer.

That constraint cuts in a helpful direction. In operand position it does not
matter whether a word is also a mnemonic, because it is being used as a name
either way. So operand-position classification does not need the instruction
table at all.

## Three ways to do it

### A. Split the reserved table by position

Two tables instead of one:

    starters   mnemonics + directives      (~240 entries)
    operands   registers + flags           (~30 entries)

The parser already knows which it is asking for: `pr_parse` is at a statement
boundary, `op_parse` and `operand_at` are not. Passing that down means each
lookup consults only the table that could match.

What it buys is not a shorter chain -- the hash is O(1) either way -- but the
chance to stop hashing at all in operand position. Thirty entries of one to
three characters is a direct dispatch: length, then first character, then at
most a two-character compare. That is the 7,699 register and 1,045 flag lookups
answered without touching a hash table, and it is the same class of change as
the macro reject, which beat its host prediction.

The remaining statement-start lookups stay hashed, but there are far fewer of
them -- roughly one per statement rather than one per identifier.

Risk: moderate. `lex_next` gains a parameter and the parser has to be honest
about position. Nothing about the token stream changes, so every existing test
still means what it meant.

### B. Resolve registers in the lexer without a table

A subset of A: leave `words` alone, but before consulting it, try a direct
match against the register and flag names. Rejects nothing, but answers 33% of
classification lookups without a hash.

Cheaper to build than A and captures most of the same win, because registers
and flags are where the dense short names are. But it makes every *other*
lookup slightly more expensive -- a failed direct match before the hash -- and
23.2% of lookups are user names that would pay that for nothing.

Worth measuring as the first step of A rather than as an alternative to it.

### C. A positional state machine, as ez80asm has

The parser tells the lexer what shape to expect and the lexer stops producing a
context-free token stream at all. This is what makes ez80asm need one hash
lookup per line where zap needs 4.2 per statement.

**Do not start here.** The scanner rewrite earlier this year was this shape and
came out 6-9% *slower*, twice, before being abandoned -- it added a pass without
removing one. The lesson recorded then applies exactly: a restructuring that
does not delete work is not an optimisation, however much better it looks.

C also destroys the property that makes the current design safe: every caller
of `lex_next` can rely on `tk_` meaning the same thing. Changing that is a
rewrite of the parser's interface, not of the lexer.

## Recommendation

Take B, measure it, and only then decide whether A is worth the interface
change. B is a strictly local change to `lex_next` with no new parameter and no
new invariant, so if the target says it is worth less than the host suggests --
as it did for the lexer cursor, 5% against a predicted 11% -- it can be dropped
without having spent anything.

If B pays, A is the same idea with the parser's knowledge added, and B's code
becomes A's operand-position path.

## How to know it worked, and how to know it did not

The benchmark set, on the Agon, per `test/bench/README.md`. Specifically:

  - **bbcbasic** should move most: it is where the lookups were counted.
  - **synth** is the control for the register path. It is dense in registers
    and has almost no user symbols, so a register fast path should show there
    even more clearly than on bbcbasic. If synth does not move, the change is
    not doing what this note claims.
  - **rokky** should move a little, in proportion.

Host instruction counts are for locating work, not sizing it. Two changes this
week: the macro reject was predicted at 2.7% and measured 3.9%; the lexer cursor
was predicted at 11% and measured 5.0%. The instrument errs in both directions.

## What this does not solve

Even at its best this is worth a few percent, and the goal is to halve zap's
time against ez80asm -- from 0.85x to 0.50x on bbcbasic, another 41%. Hashing is
15.6% of host instructions and this addresses part of that.

Getting to half is not going to come from removing lookups. It needs something
that removes a whole category of work, and the honest list of those is short:
the output buffer (streaming, deferred), `enc_instruction`'s per-row scan, and
`lex_next` itself, which is 32.9% and has resisted three rounds. This note is
worth doing, and it is not the answer.
