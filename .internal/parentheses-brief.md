# Expressions: precedence and parentheses

## What this asks for

Two changes to zap's expression evaluator:

1. **Operator precedence.** `*` and `/` before `+` and `-`.
2. **Parentheses.** Grouping, to any depth.

The reference has neither, and the second is the well-known half. The first is
the one worth reading about, because it is silent.

## Three models, not two

There are three ways to read `1+2*3` and all three are in play:

| | `1+2*3` | `4&3+1` | `WHERE>>8+128` |
|---|---|---|---|
| **ZDS** -- two levels: `* / MOD` tightest, everything else flat left-assoc | 7 | 1 | `(WHERE>>8)+128` |
| **ez80asm** -- no precedence, strictly left to right | 9 | 1 | `(WHERE>>8)+128` |
| **C** -- five levels below `* /` | 7 | 4 | `WHERE>>(8+128)` |

ZDS's model was read off ZDS directly:

    1+2*3   = 7    `*` binds tighter than `+`
    4&3+1   = 1    `&` and `+` are one level, so (4&3)+1
    1|6&4   = 4    and `|` and `&` too, so (1|6)&4
    6^3&2   = 0    (6^3)&2
    1<<2+1  = 5    (1<<2)+1
    8/2&3   = 0    but `/` still binds tighter

So ZDS and C **agree** wherever only `+ - * /` are involved, and **differ**
wherever a bitwise or shift operator meets an arithmetic one.

`zds2ez80` regroups for this: it parses with ZDS's rules, prints with C's, and
adds parentheses only where C would otherwise regroup. Over the corpus's 6857
pass-1 expressions it changes **nothing** -- they all read the same either way
-- which is the useful result. It changes exactly one instruction operand, and
that one is a warning in its own right; see below.

## ez80asm evaluates strictly left to right

No precedence at all. Measured by assembling each expression with the
reference and with ZDS and reading the bytes out of both:

| expression | ZDS | ez80asm |
|---|---|---|
| `1+2*3` | **7** | **9** |
| `2+3*4` | **14** | **20** |
| `10-2*3` | **4** | **24** |
| `5+100/10` | **15** | **10** |
| `2*3+4*5` | **26** | **50** |
| `1+2+3*4` | **15** | **24** |
| `100/10+5` | 15 | 15 |
| `2*3+4` | 10 | 10 |
| `20-4-3` | 13 | 13 |
| `1+2-3` | 0 | 0 |

They agree only where left-to-right happens to coincide with precedence --
that is, where no `*` or `/` follows a `+` or `-`.

**Nothing rejects this.** Both assemblers accept the expression and produce
different numbers. A program converted from ZDS is silently wrong wherever it
mixes the two levels.

It bites in the corpus. BBC BASIC has, in `equs.inc`:

    OC:  EQU  STAVAR+15*4

With `STAVAR = 0x400`:

    ZDS      0x400 + 60     = 0x043C = 1084
    ez80asm  (0x400 + 15)*4 = 0x103C = 4156

Six occurrences over four files do this -- `STAVAR+15*4`, `STAVAR+16*4`,
`FUNTOK+(FUNTBL_END-FUNTBL)/2` and its `/3` sibling. Small in number, and
every one of them a wrong constant in a working program.

## Parentheses: the reference has none

Not partial support, none:

    A: EQU (A+B)      Unknown identifier '(A'
    A: EQU 1+(2)      Unknown identifier '(2)'
    A: EQU 100 / (5)  SIGFPE -- the assembler crashes

`100 / 5` assembles; nothing parenthesised does. **The crash is worth
reporting upstream to envenomator on its own account** -- a parenthesised
divisor kills the process rather than diagnosing anything.

`zds2ez80` already strips a pair that wraps a whole expression, since that
changes no meaning, which covers 47 of the corpus's 59 parenthesised pass-1
constructs. The remaining 12 are real sub-expressions and no conversion
reaches them.

## A warning: C precedence breaks TinyBASIC

`sijnstra-agon-projects/TinyBASIC` assembles under ez80asm today, to 30,464
bytes. It is the program `.internal/performance-notes.md` picked out as the
benchmark worth adding. Its `DWA` macro is:

    MACRO   DWA     WHERE
            DB   WHERE >> 8 + 128
            DB   WHERE & 0FFH

With `WHERE = 0x1234`:

    ZDS, and ez80asm as it is    (0x1234 >> 8) + 128  = 0x92
    C precedence                 0x1234 >> (8 + 128)  = 0x00

Flat left-to-right gets this **right**, by accident, because it happens to
agree with ZDS here. **C precedence gets it wrong**, and nothing diagnoses it
-- the program assembles and every high byte of its jump table is zero.

So adopting C precedence is not purely an improvement. It fixes `1+2*3` and
breaks `WHERE>>8+128`, and one of those is in the corpus. If zap takes C's
rules, TinyBASIC has to go through `zds2ez80` first, which rewrites that line
to `(WHERE >> 8) + 128` -- correct under both. Worth a test either way, since
it is the one known case.

## What this is worth

Measured over the 86-project Agon corpus, converted by `zds2ez80` and
assembled with the reference. Of the 28 ZDS projects with a `.zdsproj`, 17 are
C plus assembly -- impossible, ez80asm has no C compiler -- and 11 are
assembly-only. Of those 11: **3 assemble today and 8 are blocked on
parentheses and nothing else.**

So parentheses unblock 8 real Agon programs, and precedence fixes 6 wrong
constants in the 3 that already build.

## The decision this forces

zap's standing rule is: *anything ez80asm rejects is out of scope, and
anything it accepts must come out byte-identical.*

Both changes break the second half of that, deliberately:

* With parentheses, zap accepts programs ez80asm rejects. That only widens
  what assembles, and nothing that worked before changes.
* With precedence, **zap computes different numbers than ez80asm for
  expressions ez80asm accepts.** `1+2*3` becomes 7 where the reference says 9.
  That is a real divergence on shared input, and it is the one to think hard
  about -- in both directions, since it corrects `STAVAR+15*4` and breaks
  TinyBASIC's `WHERE >> 8 + 128`.

The case for doing it anyway: left-to-right evaluation is not a dialect
choice, it is the absence of one. ZDS, every C compiler, and every other Z80
assembler in common use observe precedence; a program written anywhere else
and assembled by ez80asm gets silently wrong numbers. Following the reference
here means reproducing a defect.

The case against: zap's whole value is agreeing with the reference, and this
is the first place that would knowingly stop.

Either way it should be a decision on the record, not a side effect. If
precedence goes in, `test/corpus` -- ez80asm's own vendored suite -- is the
thing to check first: anything there that mixes `+` with `*` will change.

## Use ZDS as the oracle, not ez80asm

The reference cannot referee this: it rejects every parenthesised case and is
the thing under question on precedence.

ZDS II is installed at
`/mnt/d/Zilog/ZDSII_eZ80Acclaim!_5.3.5/bin/ez80asm.exe`. It is a Windows
binary and runs directly under WSL, given a working directory Windows can see
(`%TEMP%` will do; a `\\wsl.localhost\...` path will not).

    ez80asm.exe -cpu:eZ80F92 case.asm

Read the bytes out of the `.lst` listing rather than the `.obj`, and no linker
is needed. The listing is fixed width: PC in columns 0-5, emitted bytes in
7-26. There is a working parser in `zds2ez80/tests/support.py` (`zds_bytes`),
and `zds2ez80/tests/test_zds_bytes.py` shows the shape of a differential test.

Beware the name: ZDS's assembler is *also* called `ez80asm`. It is not the
reference.

## Operators, and where the two already differ

Inside parentheses the corpus uses `+ - * /`, `&`, unary `-`, `$` for the
program counter, and character literals.

Precedence differences beyond the arithmetic one above:

| expression | ZDS | ez80asm | |
|---|---|---|---|
| `4&3*2` | 4 | 0 | ZDS binds `*` tighter than `&`; the reference does not |
| `1\|6&4` | 4 | 4 | agree |
| `6^3&2` | 0 | 0 | agree |
| `1<<2+1` | 5 | 5 | agree |
| `~0&0FFh` | 255 | 255 | agree |
| `-3+5` | 2 | 2 | agree |

ZDS also has operators the reference has not: `MOD`, `SHL`, and `==` outside
an `IF`. None of them appear in the Agon corpus except in prose, so they are
not urgent -- but `==` does work inside `IF` in both, which is what
`zds2ez80` relies on when it strips `IF( 1 == X )` down to `IF 1 == X`.

## What must not change

**Parentheses already mean indirection**, and that is not what this touches:

    ld b, (hl)          ld a, (var)         out0 (128), a
    ld a, (ix + off)    ld hl, (counter+BASE)

All of those assemble today and must keep doing so. The change is to the
expression evaluator, not to operand parsing -- which is also why `zds2ez80`
only strips redundant parentheses from `EQU`, `DS`, `ALIGN` and `IF`, and
never from an instruction operand.

## Test material

Expected values are ZDS's, with `BASE_CLOCK=18432000, ROWS=10, COLS=20,
TCMD=7Fh`, and `start`..`tbl_end` spanning 8 bytes:

| expression | value |
|---|---|
| `BASE_CLOCK / (16 * 500000)` | 2 |
| `(ROWS+2)*(COLS+1)+1` | 253 |
| `2*(54+2)` | 112 |
| `('*'-TCMD) & 0FFH` | 171 |
| `0+('<'-4) & 0FH` | 8 |
| `((ROWS))` | 10 |
| `(ROWS+COLS)/3` | 10 |
| `-(ROWS-COLS)` | 10 |
| `($ - start) / 2` | 4 |
| `STAVAR+15*4` with STAVAR=0x400 | 1084 |

And the real ones, verbatim from the corpus -- 20 distinct expressions over 57
occurrences, of which these are the shapes:

    BAUD_500000:  EQU  BASE_CLOCK / (16 * 500000)        sijnstra, x8
    mos_api_block1_size: EQU ($ - mos_api_block1_start) / 2   MOS, x3
    TCMD:         EQU  FUNTOK+(FUNTBL_END-FUNTBL)/2      BBC BASIC
    TOT_CELLS:    EQU  (ROWS+2)*(COLS+1)+1               craiglp Life
    OC:           EQU  STAVAR+15*4                       BBC BASIC
                  CP   ('*'-TCMD) & 0FFH                 BBC BASIC, x6
                  LD   B,2*(54+2)                        BBC BASIC
                  ADD  A,(RTABLE-DTABLE)/2               BBC BASIC

Reproduce any of it with `zds2ez80 --project` against `~/agon-corpus`; see
that repository's BRIEFING.md for how the corpus is fetched and swept.
