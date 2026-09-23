# Advanced eZ80 C Optimization Guide

Notes on writing fast C for the Zilog eZ80 in ADL (24-bit) mode, using
agondev's clang at `-Oz`.

Almost all of this came from building [zap](README.md), an assembler that runs
on the Agon Light; [docs/DESIGN.md](docs/DESIGN.md) shows how zap puts it to
use. Each claim comes from one of three places: Zilog's instruction tables, a
measurement on an emulated Agon running at its real clock speed, or reasoning
that hasn't been checked. Section 4 sorts them, and section 5 describes how
the measurements were taken.

If you only read one section, read 1a. Ordinary-looking C that compiles to
library calls is where the big wins are, and you can't see it in the source.

## Contents

**[0. Terms used here](#0-terms-used-here)**

- [Registers and the index registers](#registers-and-the-index-registers)
- [The stack frame and frame pointer](#the-stack-frame-and-frame-pointer)
- [Prologue and epilogue](#prologue-and-epilogue)
- [Why 128 bytes matters](#why-128-bytes-matters)
- [Spilling](#spilling)
- [Register allocation](#register-allocation)
- [Helper calls](#helper-calls)
- [Inlining and outlining](#inlining-and-outlining)
- [Tail calls](#tail-calls)
- [`-Oz` and `-S`](#-oz-and--s)

**[1. Choosing integer widths](#1-choosing-integer-widths)**

- [Narrowing only pays if everything stays narrow](#narrowing-only-pays-if-everything-stays-narrow)

**[1a. C that compiles to calls instead of instructions](#1a-c-that-compiles-to-calls-instead-of-instructions)**

- [The usual suspects](#the-usual-suspects)
- [Fixes, most common first](#fixes-most-common-first)
- [The catch with fix 4](#the-catch-with-fix-4)
- [Finding them](#finding-them)

**[2. Architecture rules](#2-architecture-rules)**

- [Stack locals are faster than globals](#stack-locals-are-faster-than-globals)
- [Count loops down to zero](#count-loops-down-to-zero)
- [Use the 8-bit multiplier](#use-the-8-bit-multiplier)
- [Keep hot functions to a few arguments](#keep-hot-functions-to-a-few-arguments)
- [Pass structs by pointer](#pass-structs-by-pointer)
- [Branchless isn't always better](#branchless-isnt-always-better)
- [One index register per loop](#one-index-register-per-loop)

**[2a. Inlining is only a request](#2a-inlining-is-only-a-request)**

- [One cold caller can de-inline a hot helper everywhere](#one-cold-caller-can-de-inline-a-hot-helper-everywhere)
- [Don't inline a cold function with a big buffer into a hot one](#dont-inline-a-cold-function-with-a-big-buffer-into-a-hot-one)
- [Tail calls keep a frame off the common path](#tail-calls-keep-a-frame-off-the-common-path)
- [Code that's merely present still costs](#code-thats-merely-present-still-costs)
- [Sometimes a call is cheaper than a bigger frame](#sometimes-a-call-is-cheaper-than-a-bigger-frame)

**[3. Memory and arithmetic](#3-memory-and-arithmetic)**

- [Mark constant data `const`](#mark-constant-data-const)
- [Shifts: prefer bytes and tables](#shifts-prefer-bytes-and-tables)
- [Even a constant shift by two costs a call](#even-a-constant-shift-by-two-costs-a-call)
- [Use `memcpy`, `memmove` and `memset`](#use-memcpy-memmove-and-memset)
- [Use power-of-two sizes to avoid division](#use-power-of-two-sizes-to-avoid-division)
- [Inline small hot functions](#inline-small-hot-functions)
- [Keep every stack frame under 128 bytes](#keep-every-stack-frame-under-128-bytes)
- [Buffer file writes](#buffer-file-writes)

**[3a. Memory without virtual memory](#3a-memory-without-virtual-memory)**

- [A `realloc` that moves needs both copies](#a-realloc-that-moves-needs-both-copies)
- [Allocating is cheap; touching memory isn't](#allocating-is-cheap-touching-memory-isnt)
- [Measure memory like you measure time](#measure-memory-like-you-measure-time)

**[3b. Two miscompiles to know about](#3b-two-miscompiles-to-know-about)**

- [Unbounded character scans can be compiled rotated](#unbounded-character-scans-can-be-compiled-rotated)
- [A backwards trim reads one byte too far](#a-backwards-trim-reads-one-byte-too-far)
- [What they have in common](#what-they-have-in-common)

**[3c. Width and sign](#3c-width-and-sign)**

- [Comparing different widths tests the wrong bytes](#comparing-different-widths-tests-the-wrong-bytes)
- [Signed comparisons are calls](#signed-comparisons-are-calls)
- [Out-parameters force values into memory](#out-parameters-force-values-into-memory)

**[4. What's verified](#4-whats-verified)**

- [Checked against Zilog UM0077](#checked-against-zilog-um0077)
- [Measured on an emulated Agon](#measured-on-an-emulated-agon)
- [Contradicted by measurement](#contradicted-by-measurement)
- [Plausible but unverified](#plausible-but-unverified)

**[5. How to measure on this target](#5-how-to-measure-on-this-target)**

- [The host isn't a stand-in](#the-host-isnt-a-stand-in)
- [Use the program's own clock, and don't unthrottle](#use-the-programs-own-clock-and-dont-unthrottle)
- [Repeat short runs and add them up](#repeat-short-runs-and-add-them-up)
- [One change at a time, nothing else running](#one-change-at-a-time-nothing-else-running)
- [Measuring a piece of code without changing it](#measuring-a-piece-of-code-without-changing-it)
- [Read the generated assembly](#read-the-generated-assembly)

## Index by symptom

| what you're seeing | where to look |
|---|---|
| `call __imulu` / `__ishl` / `__iand` in the assembly | [The usual suspects](#the-usual-suspects) |
| an array subscript or `p += n` is slow | [The usual suspects](#the-usual-suspects), [Even a constant shift by two costs a call](#even-a-constant-shift-by-two-costs-a-call) |
| a shift by a constant isn't free | [Shifts: prefer bytes and tables](#shifts-prefer-bytes-and-tables) |
| `call pe, __setflag` in a loop | [Signed comparisons are calls](#signed-comparisons-are-calls) |
| a function got slower when an unrelated one was added | [One cold caller can de-inline a hot helper](#one-cold-caller-can-de-inline-a-hot-helper-everywhere), [Register allocation](#register-allocation) |
| a loop got slower and its source didn't change | [Code that's merely present still costs](#code-thats-merely-present-still-costs), [One index register per loop](#one-index-register-per-loop) |
| `lea hl, ix + 0` sequences everywhere | [Why 128 bytes matters](#why-128-bytes-matters), [Keep every stack frame under 128 bytes](#keep-every-stack-frame-under-128-bytes) |
| a helper marked `static inline` is being called | [Inlining and outlining](#inlining-and-outlining), [One cold caller can de-inline a hot helper](#one-cold-caller-can-de-inline-a-hot-helper-everywhere) |
| a disabled feature still costs time | [Don't inline a cold function with a big buffer](#dont-inline-a-cold-function-with-a-big-buffer-into-a-hot-one), [Code that's merely present still costs](#code-thats-merely-present-still-costs) |
| a value keeps being written to the frame and read back | [Spilling](#spilling), [Out-parameters force values into memory](#out-parameters-force-values-into-memory) |
| wrong bytes on the target, right bytes on the host | [Two miscompiles to know about](#3b-two-miscompiles-to-know-about), [Comparing different widths](#comparing-different-widths-tests-the-wrong-bytes) |
| a scan skips its first character | [Unbounded character scans can be compiled rotated](#unbounded-character-scans-can-be-compiled-rotated) |
| out of memory while growing a buffer | [A `realloc` that moves needs both copies](#a-realloc-that-moves-needs-both-copies) |
| which integer width to use | [Choosing integer widths](#1-choosing-integer-widths), [Narrowing only pays if everything stays narrow](#narrowing-only-pays-if-everything-stays-narrow) |
| should this branch become a table | [Branchless isn't always better](#branchless-isnt-always-better) |
| how to measure any of this | [How to measure on this target](#5-how-to-measure-on-this-target), [Read the generated assembly](#read-the-generated-assembly) |
| is this claim actually verified | [What's verified](#4-whats-verified) |

---

## 0. Terms used here

Most of the advice is about what the compiler emits, so it uses machine terms
rather than C terms. Skim this section if any are unfamiliar; nothing later
needs more assembly than this.

### Registers and the index registers

The eZ80 has a handful of 24-bit registers (`HL`, `DE`, `BC`, `IX`, `IY`) and
an 8-bit accumulator, `A`. A value in a register is free to use; a value in
memory costs a load.

`IX` and `IY` are the index registers, the only ones that support `(IX + d)`
addressing: a base address plus a small constant offset in a single
instruction. The compiler relies on this for structs, arrays and local
variables.

### The stack frame and frame pointer

A function's local variables live in a block of stack memory called its stack
frame. The compiler keeps `IX` pointing at it (the frame pointer), so a local
is just an offset:

```
    ld   hl, (ix - 9)      ; read a local 9 bytes into the frame
```

### Prologue and epilogue

The code at the start of a function that sets up its frame, and at the end
that tears it down. With this compiler the prologue is a call:

```
_my_function:
    ld   hl, -24           ; a 24-byte frame
    call __frameset        ; point IX at it and reserve the space
    ...
```

Every local access afterwards is relative to `IX`, so the size of the frame
decides whether those accesses are cheap.

### Why 128 bytes matters

The `d` in `(IX + d)` is a signed byte, −128 to +127. A local further into the
frame than that can't be reached directly, so the compiler computes its
address instead:

```
    ld   bc, -139          ; four instructions and a clobbered register
    lea  hl, ix + 0        ; where there was one
    add  hl, bc
    ld   hl, (hl)
```

You pay that on every access to every local past the boundary. It's the
easiest large regression to introduce by accident, because nothing in the C
tells you a frame got bigger.

### Spilling

When the compiler wants a value in a register but can't keep it there, it
writes it to the frame and reads it back later. That's a spill. Taking a
local's address (`&x`, including passing `&x` to a function) forces one, since
registers don't have addresses. So does needing more live values than there
are registers. Inside a loop, a spill costs you on every iteration.

### Register allocation

The compiler's choice of which values live in registers and which get
spilled. It's made for a whole function at once, so adding code that never
runs (an untaken branch, an inlined helper that's never reached) can slow down
a loop somewhere else in the same function. There are several examples below.

### Helper calls

The eZ80 has no multiply wider than 8 bits, no divide, no barrel shifter and
an 8-bit ALU. When C asks for something the chip can't do in one instruction,
the compiler calls a library helper whose name starts with `__`:

| in the assembly | what the C looked like |
|---|---|
| `call __imulu` | a multiply, including an array subscript |
| `call __ishl`, `call __bshl` | a shift |
| `call __iand`, `call __ior` | a bitwise operation on a 24-bit value |
| `call __idivu`, `call __irems` | `/` or `%` |
| `call pe, __setflag` | fixing the flags after a signed comparison |
| `call __l...` | anything on a 32-bit type |

Section 1a is about finding and removing these. `__frameset` is the
exception: it's the frame prologue, not arithmetic.

### Inlining and outlining

Inlining copies a function's body into its caller; outlining emits the
function once and calls it. `static inline` is only a request, and section 2a
covers when the compiler ignores it and what that costs.

### Tail calls

A call whose result is returned straight away (`return f(x);`), so nothing in
the caller needs to survive it. The caller doesn't have to save registers and
its frame can be reused, which makes a tail call much cheaper than an ordinary
one.

### `-Oz` and `-S`

`-Oz` optimizes for size. It's what agondev builds with and what every
measurement here used. `-S` writes assembly instead of an object file, which
is how you see any of the above:

```
    ez80-none-elf-clang -mllvm -z80-gas-style -Oz -S file.c -o file.s
```

---

## 1. Choosing integer widths

The width of your integers matters more than almost anything else.

**24-bit (`int`, `int24_t`) is the native size.** In ADL mode `HL`, `DE` and
`BC` are 24 bits wide, so 24-bit arithmetic maps straight onto instructions.
Use it for pointers, array indexes and general arithmetic. A 16-bit index has
to be extended to 24 bits before it can be used in an address.

**8-bit (`char`, `uint8_t`) is fastest for small values.** Underneath, the eZ80
is still an 8-bit chip, and 8-bit values sit directly in `A`, `B`, `C` and so
on. Use them for counters and flags that stay under 256.

**16-bit (`short`, `int16_t`) is probably the slowest.** There's no cheap way to
keep a 16-bit result tidy inside a 24-bit register, so the compiler is
believed to add masking and extension around it. That isn't verified (see
section 4), but there's rarely a reason to use 16-bit types except for file
formats and hardware registers.

| type | width | performance | use for |
| :--- | :--- | :--- | :--- |
| `int8_t` / `uint8_t` | 8-bit | excellent | loop counters, flags, small buffers |
| `int16_t` / `uint16_t` | 16-bit | poor (unverified) | avoid |
| `int24_t` / `uint24_t` / `int` | 24-bit | excellent | pointers, indexes, general arithmetic |

### Narrowing only pays if everything stays narrow

A narrower field isn't automatically faster. What matters is the width of
everything that touches it: if a narrowed value meets a wider one in an
expression, C widens it again, and you pay for that conversion at every use.
That can cost more than the wider field ever did.

Measured in zap on the Agon, narrowing 32-bit fields to 24 bits:

| what was narrowed | what reads it | result |
| :--- | :--- | :--- |
| `operand.imm` | shifts and masks that stay 24-bit | −0.8% on synth |
| `operand.reg` alone | compared with a 32-bit table field, so widened again | +2.4% on synth |
| `operand.reg` and the table field | nothing widens, but a shared struct is repacked | +1.0% on synth |

The same idea, applied three ways, gained 0.8% or cost 2.4% depending only on
what the field was used with. The middle row is the trap: it looks the most
obviously correct and is the worst of the three.

So:

- Only narrow a field if every operation on it narrows too. Half a conversion
  is worse than none.
- Be careful narrowing a field in a shared struct. Repacking moves every field
  after it, and the cost lands on code that has nothing to do with the change.

---

## 1a. C that compiles to calls instead of instructions

This is the most useful section in the guide. On a 24-bit machine with an
8-bit ALU, no barrel shifter and only an 8-bit multiplier (`MLT`), a lot of
ordinary C has no instruction to compile to, so the compiler calls a library
helper instead. Nothing in the source hints at it. The code looks like
arithmetic, and on the host it is arithmetic, so a host profiler won't show it
and code review won't catch it.

Every large speedup in zap was one of these, and every one was found by
reading the generated assembly rather than staring at the C.

### The usual suspects

| what you write | what you get | why |
|---|---|---|
| `x << 3`, `x << 4`, `x >> 4` | `call __bshl` / `__bshru` | no barrel shifter, so a shift is a loop over bits |
| `x << 8`, `x << 16` | `call __ishl` | even on byte boundaries, a left shift isn't turned into a byte move |
| `(uint8_t)(v >> 16)` with `v` in a register | `call __ishru` | only `>> 8` gets the byte trick (`ld a, h`); `HL`'s top byte isn't addressable |
| `a & b` on a 24-bit value | `call __iand` | `AND` is an 8-bit instruction |
| `a \| b` on a 24-bit value | `call __ior` | same |
| `x != 0` on a 24-bit value | `call __lcmpzero` | same |
| `arr[i]` where `sizeof(*arr) != 1` | `call __imulu` | the subscript is `i * size`, and `MLT` is 8-bit |
| `p += n` on a pointer to a struct | `call __imulu` | same, and easy to miss because it looks like pointer arithmetic |
| anything on `uint32_t` / `long` | `call __l*` | twice the machine's width |
| `x / y`, `x % y` | `call __idivu` / `__irems` | no divide instruction at all |

### Fixes, most common first

1. Precompute into a table. Write `shl3[i & 7]` instead of `i << 3`: an indexed
   load from a table of 256 bytes or less is one instruction. Repeated
   addition doesn't help, because the compiler turns `x+x+x+x` back into a
   shift.
2. Split wide values into bytes where they're created. A 24-bit mask that's
   only ever masked or tested for zero should be three `uint8_t`s. Split it
   where it's produced, not where it's used, or every user pays.
3. Keep constants constant. A constant shift only folds while the value is
   still known at compile time. After a `switch` merges, `bit >> 16` is a
   runtime shift and therefore a call; move the stores into each case instead.
4. Hand out pointers that already exist. Returning a pointer into a data
   structure avoids the multiply a subscript needs. But see the next heading.
5. Read bytes from memory, not from a local. `(uint8_t)(op->imm >> 16)` is an
   indexed load; copy `op->imm` into a local first and it becomes a call.

### The catch with fix 4

Handing out an existing pointer doesn't mean inventing objects to point at.
Returning pointers to twenty-eight `static const` descriptors removed a
`__ishru` call but still lost 0.4%, because the compiler hoisted their
addresses into the prologue (`ld de, _rd_a; ld (ix - 17), de`), paid on every
call, and the frame grew past a size that mattered. Removing the same call by
moving stores into the switch cases, without creating anything new, won 4.9%.
Ask where the pointer comes from.

### Finding them

```
    ez80-none-elf-clang ... -S file.c -o file.s
    grep -o 'call[ \t]*__[a-z0-9_]*' file.s | sort | uniq -c
```

Anything other than `__frameset` is an operation the chip doesn't have. Check
before optimizing and again after: several of these appeared because of
changes that looked like improvements.

---

## 2. Architecture rules

### Stack locals are faster than globals

A common piece of advice says to prefer statics, because the eZ80 has no
stack-pointer-plus-offset addressing. That's half true. There's no
`ld hl, (sp+3)`, only `ld hl, (sp)` and `add hl, sp`, and `lea` only works on
`IX` and `IY`, which is exactly why the compiler uses `IX` as a frame pointer.
But `IX` is set up once per function, and after that every access is cheaper
than an absolute address. From the Attributes tables in Zilog UM0077, in ADL
mode:

| access | instruction | cycles | bytes |
| :--- | :--- | ---: | ---: |
| static / global | `LD HL, (Mmn)` | 7 | 4 |
| stack local via IX | `LD HL, (IX+d)` | 6 | 3 |
| stack local via IY | `LD HL, (IY+d)` | 5 | 3 |
| static / global | `LD rr, (Mmn)` | 8 | 5 |
| stack local via IX | `LD rr, (IX+d)` | 6 | 3 |

In practice, making a hot 2 KB parser struct `static` instead of a local
changed its runtime by nothing (87.90s both ways, over three runs), and it
costs re-entrancy. What matters is size: a 41 KB struct on the stack is 41 KB
of memory touched, and shrinking it to 2.2 KB was free.

A struct reached through a pointer parameter is a different matter. Passing
`state*` everywhere means every field access loads the pointer from the frame
first, and it ties up an index register a loop could have used. Making the
same object a file-scope global, so its fields have fixed addresses, was worth
4.9% in zap. The real comparison isn't local versus static; it's a fixed
address versus a base pointer you have to load and keep.

### Count loops down to zero

An incrementing loop like `for (uint8_t i = 0; i < 10; i++)` needs a `cp 10`
on every iteration. Counting down (`for (uint8_t i = 10; i > 0; i--)` or
`while (--i)`) lets the zero flag from the decrement end the loop, with no
comparison.

### Use the 8-bit multiplier

The eZ80 has `MLT`, which multiplies two 8-bit registers into a 16-bit result
(`HL = H * L`). Keep multiplication operands in `uint8_t` and the compiler can
use it; multiply two 24-bit values and it falls back to a library routine.

### Keep hot functions to a few arguments

Arguments in registers are the cheapest. Keep performance-critical inner
functions to two or three arguments so they fit in `HL`, `DE` and `BC`, and
check your compiler's calling convention for the details.

### Pass structs by pointer

Passing a struct by value copies it onto the stack with `LDIR`. Pass a pointer
instead.

### Branchless isn't always better

On a chip with no branch predictor, the usual advice is to replace `if`/`else`
chains with tables or flag arithmetic. That's right when a branch just picks
between two values that cost about the same.

It's wrong when the branch skips work. zap's instruction-row matcher used to
compute every part of its test so the whole thing could be one branch.
Rejecting candidates on the cheapest test first, and only then doing the
expensive ones, was worth 6.4% on zap's instruction-heavy benchmark (and 23.2%
on the stripped-down build where it was first measured).

The question is whether the branch avoids computation. If it does, keep it.

### One index register per loop

`IX` is the frame pointer, which leaves `IY` as the only register for walking
a struct in a loop. Anything that needs a second computed address inside that
loop evicts the pointer to the frame and reloads it on every use.

zap's row test read three adjacent bytes of a row and ANDed each with the
matching byte of the operand. Only one of the three can be non-zero, so the
obvious improvement was to store which one and read just that byte:
`(&ri->a0)[plane]`. Three loads and three ANDs become one of each.

It cost 6.2%. The variable index has to be added to the row pointer, so the
pointer has to be in `HL`, so it's no longer in `IY`, and every other field
access (and the loop itself) goes through the frame:

```
ld  iy, (ix - 42)     ; reload the row pointer
ld  bc, (ix - 48)     ; the plane index
add iy, bc
ld  a, (iy + 2)
ld  iy, (ix - 42)     ; and put it back for the next field
```

The original version was two instructions per byte,
`ld a, (iy+n); and a, (ix+m)`, with no address arithmetic at all. Fewer
operations lost to more addressing.

Inside a loop over a struct, prefer constant offsets from one index register
over any computed address, even one that replaces several constant offsets.
Count the reloads, not the operations.

---

## 2a. Inlining is only a request

`static inline` is advice, and on a machine where calls are expensive, whether
the compiler takes it matters a lot. These lessons all come from zap, all were
measured, and none of them is visible in the C.

### One cold caller can de-inline a hot helper everywhere

A `static inline` helper with one hot caller gets inlined. Add a second caller
anywhere, even a cold one that runs once at startup, and the compiler may emit
the function out of line and turn every call into a real call, including the
hot one.

This happened five times in zap, to five different helpers:

| helper | the cold caller responsible | cost once outlined |
|---|---|---|
| case-insensitive compare | option parsing at startup | 5.5% of runtime |
| mnemonic lookup | the mode-suffix reader | isa_degenerate 4.86s → 5.16s |
| the row matcher | the suffixed-instruction path | 4.86s → 5.80s |
| an argument parser | a second directive | measurable on every line |
| the forward-reference result | one directive | a call with three out-parameters per reference |

The fix is `__attribute__((always_inline))` on helpers that must stay inlined.
Whenever you add a caller to an existing `static inline`, check that it's
still inlined where it matters:

```
    grep -c '^_helper_name:' file.s     # 0 = inlined everywhere, 1 = outlined
```

### Don't inline a cold function with a big buffer into a hot one

The opposite problem. A function that only runs in unusual cases but has a
large local buffer adds that buffer to the frame of whatever it's inlined
into:

```
    list_args()  -- 176-byte line buffer, only used when listing
    inlined into macro_expand()  -- frame grew from 73 to 267 bytes
```

Every macro expansion then paid for a listing nobody asked for, since 267
bytes is well past what an `IX` offset can reach. Marking the cold function
`noinline` brought the frame back to 79 bytes. Think about what a cold
function adds to a hot frame, not just how often it runs.

### Tail calls keep a frame off the common path

A function that carries on after a call has to keep everything alive across
it. One that returns the call's result doesn't:

```c
    /* costs the common path: everything live across the call */
    insn = try_something(s, n, p, e, &stop);
    if (insn != NULL) { ...carry on with operands... }

    /* costs the common path nothing */
    return try_something(s, n, p, e, stop);
```

zap reaches directives, mode suffixes and three-operand forms by a tail call
once the mnemonic lookup fails. Written as a fall-through instead, the same
code cost 6% on every benchmark, even the ones with no directives.

### Code that's merely present still costs

Two hundred instructions of argument parsing in front of the line loop, never
executed on the benchmark because the option wasn't used, cost 5.3% by
disturbing the loop's register allocation. `noinline` on the parser fixed it.

More generally: with only one usable index register in a loop, anything that
makes the surrounding function longer can evict the pointer the loop walks.
Measure the loop, not the feature.

### Sometimes a call is cheaper than a bigger frame

A range check on every immediate was tried two ways. In a helper the compiler
kept as a real function, it cost a call per immediate. Moved to the call site
so there was no call, it grew `assemble_line`'s frame from 108 to 111 bytes,
and that was slower. Near the 128-byte limit, three bytes of frame can cost
more than a call per iteration. You have to measure which way it goes.

---

## 3. Memory and arithmetic

### Mark constant data `const`

Declare lookup tables, strings and fixed arrays `const`. Otherwise the compiler
may copy them into RAM at startup, wasting memory and slowing initialization.
Make sure the linker places code and read-only data in the fastest memory
available.

### Shifts: prefer bytes and tables

There's no barrel shifter, so shifting by an arbitrary amount is a loop of
one-bit shifts. Line shifts up with byte boundaries where you can, and use a
lookup table where you can't.

With agondev's clang at `-Oz`, every constant shift that isn't a whole number
of bytes is a call: `(v & 7) << 3` compiles to `ld b, 3; call __bshl`. Writing
it as repeated addition doesn't help, because `((x+x)+x)+x` and `* 8` both
turn back into a shift. A table works, since an indexed load from 256 bytes or
less is one instruction:

```c
static const uint8_t shl3[8] = { 0, 8, 16, 24, 32, 40, 48, 56 };
opcode |= shl3[index & 7];          /* not opcode |= index << 3 */
```

Replacing zap's `<< 3` and `<< 4` with 8- and 16-entry tables was worth 1.6%.

Shifts by whole bytes are cheaper but not always free. It depends on where the
value is:

| expression | value in a register | value still in memory |
|---|---|---|
| `(uint8_t) v` | `ld a, l` | `ld a, (iy+n)` |
| `(uint8_t)(v >> 8)` | `ld a, h` | `ld a, (iy+n+1)` |
| `(uint8_t)(v >> 16)` | `ld c, 16; call __ishru` | `ld a, (iy+n+2)` |

`HL`'s top byte isn't directly addressable, so the compiler can do the byte
trick for `>> 8` but not for `>> 16`. That turns the usual advice around here:
don't copy a value into a local just to pull bytes out of it. Reading the
struct field fresh for each byte keeps all three as indexed loads. That was
worth 1.5% of zap's total runtime, from one function.

### Even a constant shift by two costs a call

`x * 4` on a 24-bit value compiles to `ld c, 2; call __ishl`, and so does
`x << 2`. So does `x += x; x += x;`, because LLVM merges the two adds back into
a shift before the backend sees them. There's no way of writing "double it
twice" that survives.

In practice, turning an array index into an address always costs one helper
call, and all you get to choose is which:

| element size | what you get |
| :--- | :--- |
| 3 bytes (a bare pointer) | `call __imulu` |
| 4 bytes (a pointer plus padding) | `call __ishl` |
| 1 byte (an index, not a pointer) | nothing, but turning the index back into a pointer costs a scale |

Padding table entries to a power of two to get the cheaper helper was worth
1.8% in zap's mnemonic lookup, which runs once per source line. You can't go
further than that: storing byte indexes removes the scale from the table read
but adds the same one to the array being indexed.

Watch out for portability. `(uint8_t*) table + b + b + b` does avoid the call,
but it's wrong anywhere pointers aren't three bytes, including the host where
the unit tests run. Change the element size, not the arithmetic.

### Use `memcpy`, `memmove` and `memset`

Don't write loops to copy or clear memory. The compiler turns the library
functions into the eZ80's block instructions (`LDIR`, `LDDR`, `CPIR`,
`CPDR`). In zap, copying `INCBIN` data with `memcpy` instead of byte by byte
was 22.8% faster, and filling an `ORG` gap with `memset` instead of a loop took
a source with 96 KB of gap from 0.56s to 0.10s. That loop existed even though
this advice was already written here: a four-line fill loop doesn't look hot
until something asks it for 96 KB.

### Use power-of-two sizes to avoid division

There's no divide instruction, so `/` and `%` call library routines. Make array
sizes, row widths and ring buffers powers of two so the compiler can use a
mask (`index & 255`) instead.

Don't assume it matters until you measure, though. zap's listing formatted
each line number with `(line / 1000) % 10` and its neighbours, five library
calls per line and 37,000 for one large source. Replacing them with a
four-digit counter measured 10.80s before and 10.82s after, which is no
difference at all, and the change was reverted.

### Inline small hot functions

A call pushes a 24-bit return address and jumps, and the return pops it. For
small helpers called in inner loops, `static inline` removes that overhead;
removing one call per token in zap was worth 2.9%. The limit is the frame:
inlining only helps while the result stays under 128 bytes (see the next
section, and section 2a for the ways inlining goes wrong).

### Keep every stack frame under 128 bytes

`IX` offsets are a signed byte (section 0). A function with a frame larger than
128 bytes can't reach most of its locals with `ld a, (ix-9)`, so the compiler
computes their addresses instead:

```
    ld   bc, -139
    lea  hl, ix + 0
    add  hl, bc
    ld   hl, (hl)      ; four instructions where there was one
```

That happens on every access to every local past the limit, and nothing in
the source hints at it.

Fix it by splitting the function, or by moving the locals an inner loop uses
into a small helper. This can even make the program smaller: splitting one
149-byte frame into four of 60, 62, 19 and 20 bytes removed 23 of those
sequences and 77 instructions from the binary, despite adding four calls and
returns. It was worth 7.8% overall and 28.3% on the hottest loop.

To check, compile to assembly. `grep -c 'lea.*hl, ix + 0'` counts the computed
accesses, and `grep -A2 __frameset` shows each function's frame size.

Inlining is where this bites hardest. Those four functions weren't written
big; the compiler folded all four into `main`. At `-Oz` it will happily inline
a whole program into one frame and then pay the penalty on every local.

It can hide in code nobody measures, too. `list_line` built each listing row
in a 176-byte local buffer, which gave it a 220-byte frame, so every one of its
thirty-odd `buf[w++]` stores went through a computed address. Making the buffer
`static` shrank the frame to 29 bytes and took `-l` from 12.22s to 10.80s
(11.6%). Nothing caught it earlier because no test produced a large enough
listing.

### Buffer file writes

Every write is a MOS call, whatever its size. zap's listing was written
unbuffered, two calls per line (the text, then the newline), which for a
7,509-line source is 21,630 MOS calls. A 1 KB buffer took `-l` from 13.10s to
12.22s (6.7%).

---

## 3a. Memory without virtual memory

512 KB, no swap, no MMU. That causes problems a desktop never has.

### A `realloc` that moves needs both copies

Whether a growth succeeds depends on the peak during the copy, not the final
size. A 110 KB arena growing by eight bytes needs 228 KB while it copies, and
on a 512 KB machine that's where an assembler runs out, two thirds of the way
through a symbol table it would otherwise have finished.

zap uses two fixes:

- Grow by doubling instead of in fixed steps. Reaching 197 KB in 32 KB steps
  takes five growths, and the last one needs 192 + 224 = 416 KB. Doubling gets
  there in two, with a peak of 128 + 256 = 384 KB. The peak drops from about
  twice the final size to about 1.5 times.
- For anything that only grows, use a list of fixed blocks that never move.
  Each block is allocated, filled and never resized, so the peak is just the
  total. Pointers into it also stay valid, which saves an addition per
  comparison compared with storing offsets.

### Allocating is cheap; touching memory isn't

Replacing a 39.7 KB fixed array with per-item allocation cost nothing in time
and saved 95% of the memory. With no cache and no allocator pressure, trading
an allocation for a smaller footprint is nearly always worth it here.

Likewise, a 41 KB struct on the stack is 41 KB touched; shrinking one to
2.2 KB was free. Smaller hot data can also be faster: cutting a token from 17
bytes to 13 was worth 3.4%.

### Measure memory like you measure time

Speed gets measured constantly and memory rarely, which is backwards here: an
assembler that's 5% faster but doesn't fit isn't faster. A small allocation
shim that reports the peak costs nothing when it's off. Switch it on by
renaming the allocators on the compile line:

```
    -DZMALLOC -Dmalloc=z_malloc -Dcalloc=z_calloc \
              -Drealloc=z_realloc -Dfree=z_free
```

---

## 3b. Two miscompiles to know about

Both happen at `-Oz` with agondev's clang, both are silent, and both work
correctly on the host.

### Unbounded character scans can be compiled rotated

```c
    while (cls(*p)) { p++; }              /* the mistake */
    while (p < e && cls(*p)) { p++; }     /* the fix */
```

With a sentinel to stop it, the first loop is valid C. But the compiler may
produce a loop that pre-decrements the pointer and tests the character after
it each time, so the first character is never checked and the scan stops one
short: `ld a, 0x42` parses as `0x4` and leaves the `2` behind.

It doesn't reproduce in isolation. The same loop on its own compiles
correctly, and indexing from a base instead of advancing a pointer fails the
same way. It has happened on five different loops, and once on four loops in
a function whose source hadn't changed at all: a function was added elsewhere
and the register allocation shifted.

So every character scan in zap checks its bound. That costs 0.06s out of 5.6s,
and a test reads the source to make sure no scan is missing one.

### A backwards trim reads one byte too far

```c
    while (ae > as && is_space_ch(ae[-1])) { ae--; }   /* miscompiled at -Oz */
```

The decrement happens before the test, so the byte examined is `ae[-2]`. A
one-character macro argument sees the space in front of it, trims itself away
and expands to nothing.

The fix is not to trim backwards at all: track the last non-space character
while scanning forwards, which needs no backward index and takes one pass
instead of two.

### What they have in common

The host build is correct in both cases, under every sanitizer and at every
buffer size. Only reading the generated assembly or running on the target will
find them.

```
    ez80-none-elf-clang ... -S file.c -o file.s
    grep -B2 -A6 'dec iy' file.s      # what a rotated scan looks like
```

---

## 3c. Width and sign

### Comparing different widths tests the wrong bytes

zap evaluates expressions in 32 bits on a machine whose `int` is 24, so every
boundary needs care. Comparing a 32-bit value with a bare `int` constant
compiled into a test of the wrong part of the value, and rejected an addend of
zero on the target while the host build was fine.

Make both sides of every comparison the same width, using typed constants
rather than bare literals:

```c
    #define FIX_ADDEND_MIN ((evalue) -8388608)
    #define FIX_ADDEND_MAX ((evalue)  8388607)
```

Don't derive limits like these from `sizeof(int)`. That makes the program
accept a value on one machine and reject it on the other.

### Signed comparisons are calls

Comparing two signed `int`s with `<` can't be done with one subtraction, so the
compiler adds `call pe, __setflag` to fix up the flags on overflow. In a loop
(comparing lengths, bounding a table walk, checking for output space) that's a
call on every iteration.

Making three loop counters unsigned was worth 1.4% of a whole run. If a value
can't be negative, give it an unsigned type.

```
    grep -c 'call[ \t]*pe, __setflag' file.s
```

### Out-parameters force values into memory

A helper that advances the caller's cursor through `const char** pp` forces
that cursor into the frame, because its address has been taken. Returning the
new position instead, and letting the caller assign it, was worth 2.4% on the
data-directive path, even with the helper inlined.

The same thing happens wherever you pass `&local`, including to a function
that ends up inlined.

---

## 4. What's verified

Not every claim here is equally well supported.

### Checked against Zilog UM0077

From the instruction Attributes tables (page 79 onwards):

- Stack locals are faster than globals: 6 cycles against 7 for `HL`, and 6
  against 8 for `BC`/`DE`. An earlier version of this guide said the opposite.
- There's no divide instruction, so `/` and `%` call library code.
- `MLT` is an 8×8 multiply giving 16 bits "regardless of the ADL mode", so
  keeping factors in `uint8_t` is right.
- `LDIR` and `CPIR` exist, so `memcpy`, `memmove` and `memset` can map onto
  block instructions.

### Measured on an emulated Agon

Each figure is the program's own reported time, with the emulator running at
the real 18.432 MHz (never `-u`; see section 5). Results repeat to within about
0.25%.

| finding | effect | section |
|---|---|---|
| token struct cut from 17 to 13 bytes | −3.4% | 3a |
| one call per token removed by inlining | −2.9% | 3 |
| `INCBIN` data copied with `memcpy` | −22.8% | 3 |
| `ORG` gap filled with `memset` | 0.56s → 0.10s | 3 |
| listing output buffered | −6.7% | 3 |
| listing row buffer made `static` (frame 220 → 29 bytes) | −11.6% | 3 |
| divisions removed from listing line numbers | no change | 3 |
| constant shifts replaced by tables | −1.6% | 3 |
| not copying a value to a local before extracting bytes | −1.5% | 3 |
| table entries padded to a power of two | −1.8% | 3 |
| a 149-byte frame split into four | −7.8%, −28.3% on the hottest loop | 3 |
| a stored pointer instead of a subscript | −20.5% on one lookup | 1a |
| pointers to 28 static descriptors | +0.4% | 1a |
| a cold second caller de-inlining a helper | up to +5.5% | 2a |
| a cold 176-byte buffer inlined into a hot function | frame 73 → 267 bytes | 2a |
| tail calls instead of fall-through | −6% on every benchmark | 2a |
| file-scope state instead of a pointer parameter | −4.9% | 2 |
| cheapest rejection test first in row matching | −6.4% (−23.2% on the stripped build) | 2 |
| a computed index replacing three constant offsets | +6.2% | 2 |
| three signed loop counters made unsigned | −1.4% | 3c |
| an out-parameter replaced with a return value | −2.4% | 3c |
| host speedup compared with Agon speedup | 1.36x vs 3.20x | 5 |

A few results don't appear elsewhere:

- One struct per row, walked with a pointer, beat eight parallel `uint8_t`
  arrays: 10.1% faster overall and 35% faster on the row-heavy benchmark,
  where the parallel arrays had been 8.5% slower than the starting point.
  Offsets from `IY` share one base address; separate arrays each repeat the
  address arithmetic. (Part of that gain is also that `AND` on a `uint24_t`
  is `call __iand`, and indexing an array of them is `call __imulu`.)
- `-O2`, `-O3` and `-Ofast` were all slower than `-Oz`. With no cache, code
  size is part of speed.
- A check applied to every value costs real time (2.1% on a real program, 6.7%
  on a synthetic one), while diagnostics that only run after something has
  already failed cost nothing. That's why zap's truncation warning is behind
  `-w`.

### Contradicted by measurement

"Data-driven beats branching" is too simple. Replacing a chain of about eight
failing character comparisons with two lookups in a 256-byte table was 0.3%
slower. Replacing short-circuit `&&`/`||` with bitwise operators on values
already in registers was 0.8% faster. The rule that fits both: branches that
aren't taken are cheap, and memory accesses aren't. Replace a branch with
register arithmetic and you win; replace it with a table lookup and you may
lose.

### Plausible but unverified

- That 16-bit types are the slowest. That's about the masking the compiler
  generates, which the instruction tables can't confirm.
- The three-stage pipeline and its 1-2 cycle taken-branch penalty. It isn't in
  the instruction tables; the branching results above fit it but don't prove
  it.

---

## 5. How to measure on this target

Every number in this guide came from this method, and the method is worth as
much as the findings.

### The host isn't a stand-in

The same 135 test sources and the same two assemblers, measured both ways:

| | geometric mean speedup |
|---|---|
| on the host, x86-64 | 1.36x |
| on the Agon, 18.432 MHz | 3.20x |

That's off by a factor of 2.5, in the direction that would have made almost
every eZ80-specific change look not worth doing. Host instruction counts have
also overstated a lexer change by 3x, and once called the biggest win of a set
a regression.

Use the host for correctness and the target for speed.

### Use the program's own clock, and don't unthrottle

With `-u`, fab-agon-emulator runs as fast as the host can manage, so the
guest's `clock()` no longer reflects the work being done. Run at the real
18.432 MHz and read the time the program prints.

### Repeat short runs and add them up

The clock counts hundredths of a second, and a twenty-line source takes 2-30
ms, so it reads as `0.00` or `0.01`. Run it eight or twelve times and add up
the times: the tick falls in a different place each run, so the rounding
averages out. Eight runs of a 25 ms job add up to about 0.20s, give or take a
hundredth, which is a 5% error instead of 50%.

### One change at a time, nothing else running

- The emulator repeats to within about 0.25%: three interleaved runs of two
  binaries gave 8.24/8.26/8.24 and 8.28/8.28/8.28. Don't claim a change under
  half a percent from one run, and don't dismiss a consistent 0.4% as noise.
- Two emulators on one machine slow each other down. A forgotten benchmark
  left running spoiled a whole afternoon of numbers.
- Don't edit the source while a benchmark runs. It's easy to lose track of
  which build is being timed.

### Measuring a piece of code without changing it

Instrumenting code changes it. Adding a second call to a function that was
inlined into a 3,500-line hot function makes the compiler outline it, and the
measurement then includes the outlining.

Two techniques avoid that.

**Duplicate the work.** Build with every table entry duplicated. The walk does
twice the work, the match is still found at the first copy, and the output is
byte-identical, which confirms the measurement is valid. The extra time is the
cost of the walk:

```
    make EXTRA_CFLAGS=-DDUP_ROW      # the register test in the row matcher
    make EXTRA_CFLAGS=-DDUP_BUCKET   # the mnemonic bucket chain
```

**Vary the input.** Use one binary and several inputs of the same size that
differ in one feature: with and without macros, with and without
conditionals, with and without EQU. The difference in time is that feature's
cost, with no changes to the build.

Avoid building a series of truncated programs (`#ifdef`s that stop partway
through). Each one is a different program with its own register allocation,
not the same program with a piece removed. Two changes made on that kind of
evidence both turned out slower.

### Read the generated assembly

The single most useful habit. Before optimizing, and again after:

```
    ez80-none-elf-clang -mllvm -z80-gas-style -Oz -S file.c -o file.s

    grep -o 'call[ \t]*__[a-z0-9_]*' file.s | sort | uniq -c   # helper calls
    grep -c 'call[ \t]*pe, __setflag' file.s                   # signed compares
    grep -A2 '__frameset' file.s                               # frame sizes
    grep -c 'lea.*hl, ix + 0' file.s                           # computed accesses
    grep -c '^_helper:' file.s                                 # inlined or not
```

Several regressions in zap came from changes that looked like improvements,
and all of them showed up here first.
