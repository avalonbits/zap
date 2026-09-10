# Advanced eZ80 C Optimization Guide

A reference for writing fast C on the Zilog eZ80, in **ADL (24-bit) mode**.

Almost everything here was learned by building [zap](README.md), a one-pass
assembler that runs on an Agon Light and is about three times faster than the
assembler it replaces; [docs/DESIGN.md](docs/DESIGN.md) describes what that
program does with the advice below. Each claim is marked with where it comes from: the Zilog
instruction tables, a measurement on an emulated Agon at its real clock, or
reasoning that has not been checked. Section 4 sorts them; section 5 is the method.

**If you read one section, read 1a** -- ordinary C that has no instruction to
compile to is where the large wins are, and it is invisible in the source.

## Contents


**[0. The Words This Guide Uses](#0-the-words-this-guide-uses)**

* [Registers, and the two index registers](#registers-and-the-two-index-registers)
* [The stack frame, and the frame pointer](#the-stack-frame-and-the-frame-pointer)
* [The frame prologue and epilogue](#the-frame-prologue-and-epilogue)
* [Why 128 bytes matters](#why-128-bytes-matters)
* [Spilling](#spilling)
* [Register allocation](#register-allocation)
* [Helper calls: `__imulu`, `__ishl`, `__setflag`](#helper-calls-imulu-ishl-setflag)
* [Inlining and outlining](#inlining-and-outlining)
* [Tail call](#tail-call)
* [The `-Oz` and `-S` flags](#the--oz-and--s-flags)

**[1. Choosing the Right Data Sizes (8 vs. 16 vs. 24-bit)](#1-choosing-the-right-data-sizes-8-vs-16-vs-24-bit)**

* [24-Bit Integers (`int` or `int24_t`) -- *Fastest for Pointers & Math*](#24-bit-integers-int-or-int24t----fastest-for-pointers--math)
* [8-Bit Integers (`char` or `uint8_t`) -- *Fastest for Counters & Flags*](#8-bit-integers-char-or-uint8t----fastest-for-counters--flags)
* [16-Bit Integers (`short` or `int16_t`) -- *The Worst Performer in ADL Mode*](#16-bit-integers-short-or-int16t----the-worst-performer-in-adl-mode)
* [Narrowing pays only if the operations stay narrow](#narrowing-pays-only-if-the-operations-stay-narrow----measured)

**[1a. The Main Hazard: C That Compiles to Calls, Not Instructions](#1a-the-main-hazard-c-that-compiles-to-calls-not-instructions)**

* [The offenders](#the-offenders)
* [The fixes, in order of how often they apply](#the-fixes-in-order-of-how-often-they-apply)
* [The trap in fix 4](#the-trap-in-fix-4)
* [How to find them](#how-to-find-them)

**[2. Core Architecture Rules](#2-core-architecture-rules)**

* [Do NOT Prefer Global/Static Over Stack Locals -- Stack Access Is Faster](#do-not-prefer-globalstatic-over-stack-locals----stack-access-is-faster)
* [Structure Loops to Count Down to Zero](#structure-loops-to-count-down-to-zero)
* [Leverage the 8-bit Hardware Multiplier (`MLT`)](#leverage-the-8-bit-hardware-multiplier-mlt)
* [Pass Arguments via Registers](#pass-arguments-via-registers)
* [Avoid Passing Structures by Value](#avoid-passing-structures-by-value)
* [Branchless is not unconditional](#branchless-is-not-unconditional----measured)
* [One index register is the budget inside a loop](#one-index-register-is-the-budget-inside-a-loop----measured)

**[2a. Inlining Is a Request, and Function Shape Is a Cost](#2a-inlining-is-a-request-and-function-shape-is-a-cost)**

* [One cold caller de-inlines a hot helper for everybody](#one-cold-caller-de-inlines-a-hot-helper-for-everybody)
* [A cold function with a big buffer must not be inlined into a hot one](#a-cold-function-with-a-big-buffer-must-not-be-inlined-into-a-hot-one)
* [Tail calls keep a frame off the common path](#tail-calls-keep-a-frame-off-the-common-path)
* [Code that is merely *there* costs](#code-that-is-merely-there-costs)
* [Calls versus frames: sometimes the call is cheaper](#calls-versus-frames-sometimes-the-call-is-cheaper)

**[3. Advanced Memory & Mathematical Optimizations](#3-advanced-memory--mathematical-optimizations)**

* [Master `const` and Memory Segments (RAM vs. Flash)](#master-const-and-memory-segments-ram-vs-flash)
* [Replace Bit-Shifting with Byte-Swapping](#replace-bit-shifting-with-byte-swapping)
* [Exploit Block Memory Instructions (`LDIR` / `CPIR`)](#exploit-block-memory-instructions-ldir--cpir)
* [Use Power-of-Two Array Sizes to Avoid Division](#use-power-of-two-array-sizes-to-avoid-division)
* [Inline Small, Critical Functions](#inline-small-critical-functions)
* [Keep Every Stack Frame Under 128 Bytes](#keep-every-stack-frame-under-128-bytes)

**[3a. Memory on a Machine With No Virtual Memory](#3a-memory-on-a-machine-with-no-virtual-memory)**

* [A realloc that moves holds both copies](#a-realloc-that-moves-holds-both-copies)
* [Allocation is cheap; touching memory is not](#allocation-is-cheap-touching-memory-is-not)
* [Measure memory the way you measure time](#measure-memory-the-way-you-measure-time)

**[3b. Two Miscompiles to Know About](#3b-two-miscompiles-to-know-about)**

* [An unbounded character scan can be compiled rotated](#an-unbounded-character-scan-can-be-compiled-rotated)
* [A backwards trim reads one byte too far](#a-backwards-trim-reads-one-byte-too-far)
* [What both have in common](#what-both-have-in-common)

**[3c. Width and Sign](#3c-width-and-sign)**

* [A comparison between different widths tests the wrong bytes](#a-comparison-between-different-widths-tests-the-wrong-bytes)
* [A signed compare is a call](#a-signed-compare-is-a-call)
* [An out-parameter puts a value in memory](#an-out-parameter-puts-a-value-in-memory)

**[4. Provenance -- What Here Is Verified](#4-provenance----what-here-is-verified)**

* [Verified against Zilog UM0077 (the instruction Attributes tables, from p. 79)](#verified-against-zilog-um0077-the-instruction-attributes-tables-from-p-79)
* [Verified by measurement on an emulated Agon](#verified-by-measurement-on-an-emulated-agon)
* [Contradicted by measurement](#contradicted-by-measurement)
* [Plausible but unverified](#plausible-but-unverified)
* [A note on measuring at all](#a-note-on-measuring-at-all)
* [A constant shift is a call, and you cannot write your way out](#a-constant-shift-is-a-call-and-you-cannot-write-your-way-out----measured)

**[5. How to Measure on This Target](#5-how-to-measure-on-this-target)**

* [The host is not a proxy, and it is biased rather than noisy](#the-host-is-not-a-proxy-and-it-is-biased-rather-than-noisy)
* [Read the program's own clock, and do not unthrottle the emulator](#read-the-programs-own-clock-and-do-not-unthrottle-the-emulator)
* [The clock counts hundredths, so repeat and sum](#the-clock-counts-hundredths-so-repeat-and-sum)
* [One change, one measurement, nothing else running](#one-change-one-measurement-nothing-else-running)
* [Pricing a piece of code without instrumenting it](#pricing-a-piece-of-code-without-instrumenting-it)
* [Read the generated assembly](#read-the-generated-assembly)

## Index by symptom

| what you are seeing | where to look |
|---|---|
| `call __imulu` / `__ishl` / `__iand` in the assembly | [The offenders](#the-offenders) |
| an array subscript or `p += n` is slow | [The offenders](#the-offenders), [A constant shift is a call](#a-constant-shift-is-a-call-and-you-cannot-write-your-way-out----measured) |
| a shift by a constant is not free | [Replace Bit-Shifting with Byte-Swapping](#replace-bit-shifting-with-byte-swapping) |
| `call pe, __setflag` appears in a loop | [A signed compare is a call](#a-signed-compare-is-a-call) |
| a function got slower when an unrelated one was added | [One cold caller de-inlines a hot helper](#one-cold-caller-de-inlines-a-hot-helper-for-everybody), [Register allocation](#register-allocation) |
| a loop got slower and its source did not change | [Code that is merely *there* costs](#code-that-is-merely-there-costs), [One index register is the budget](#one-index-register-is-the-budget-inside-a-loop----measured) |
| `lea hl, ix + 0` sequences everywhere | [Why 128 bytes matters](#why-128-bytes-matters), [Keep Every Stack Frame Under 128 Bytes](#keep-every-stack-frame-under-128-bytes) |
| a helper you marked `static inline` is being called | [Inlining and outlining](#inlining-and-outlining), [One cold caller de-inlines a hot helper](#one-cold-caller-de-inlines-a-hot-helper-for-everybody) |
| a cold feature costs time even when it is switched off | [A cold function with a big buffer](#a-cold-function-with-a-big-buffer-must-not-be-inlined-into-a-hot-one), [Code that is merely *there* costs](#code-that-is-merely-there-costs) |
| a value keeps being written to the frame and read back | [Spilling](#spilling), [An out-parameter puts a value in memory](#an-out-parameter-puts-a-value-in-memory) |
| wrong bytes on the target, right bytes on the host | [Two Miscompiles to Know About](#3b-two-miscompiles-to-know-about), [A comparison between different widths](#a-comparison-between-different-widths-tests-the-wrong-bytes) |
| a scan skips its first character | [An unbounded character scan can be compiled rotated](#an-unbounded-character-scan-can-be-compiled-rotated) |
| out of memory while growing a buffer | [A realloc that moves holds both copies](#a-realloc-that-moves-holds-both-copies) |
| which integer width should this be | [Choosing the Right Data Sizes](#1-choosing-the-right-data-sizes-8-vs-16-vs-24-bit), [Narrowing pays only if the operations stay narrow](#narrowing-pays-only-if-the-operations-stay-narrow----measured) |
| should this branch be replaced by a table | [Branchless is not unconditional](#branchless-is-not-unconditional----measured) |
| how do I measure any of this | [How to Measure on This Target](#5-how-to-measure-on-this-target), [Read the generated assembly](#read-the-generated-assembly) |
| is this claim actually verified | [Provenance](#4-provenance----what-here-is-verified) |

---

## 0. The Words This Guide Uses

Most of the advice below is about what the compiler emits, so it uses terms
from the machine rather than from C. If any of these are unfamiliar, read this
section first; nothing else here depends on knowing assembly beyond it.

### Registers, and the two index registers

The eZ80 has a handful of 24-bit registers -- `HL`, `DE`, `BC`, `IX`, `IY` --
and an 8-bit accumulator `A`. A value in a register is free to use; a value in
memory costs a load.

`IX` and `IY` are the **index registers**. They are the only ones that can be
used as `(IX + d)`: a base address plus a small constant offset, in one
instruction. Everything the compiler does with structs, arrays and local
variables leans on that.

### The stack frame, and the frame pointer

When a function runs, its local variables live in a block of memory on the
stack. That block is the function's **stack frame**, and the compiler keeps a
register pointing at it -- the **frame pointer**, which on this target is `IX`.

A local variable is then an offset from that pointer:

```
    ld   hl, (ix - 9)      ; read a local 9 bytes into the frame
```

### The frame prologue and epilogue

The instructions at the top of a function that set the frame up, and the ones
at the bottom that take it down. On this compiler the prologue is a call:

```
_my_function:
    ld   hl, -24           ; this function's frame is 24 bytes
    call __frameset        ; set IX to point at it, reserve the space
    ...
```

That is why "the frame grew" is a real cost and not bookkeeping: everything the
function does afterwards is measured from `IX`, and the *size* of the frame
decides whether those accesses are cheap.

### Why 128 bytes matters

The `d` in `(IX + d)` is a **signed byte**: −128 to +127. A local more than 128
bytes into the frame cannot be reached that way, so the compiler computes its
address instead:

```
    ld   bc, -139          ; five instructions, and a register clobbered,
    lea  hl, ix + 0        ; where there was one
    add  hl, bc
    ld   hl, (hl)
```

This is paid on **every access** to every local past the boundary. It is the
single easiest large regression to introduce by accident, because nothing in
the C says a frame got bigger.

### Spilling

A value the compiler wanted to keep in a register, but could not, gets written
to the frame and read back -- it is **spilled**. Taking the address of a local
(`&x`, or passing `&x` to a function) forces a spill, because a register has no
address. So does needing more live values at once than there are registers.

Inside a loop, a spill is paid every iteration.

### Register allocation

The compiler's decision about which values live in which registers, and which
get spilled. It is made per function, over the whole function, which is why
adding code that never runs -- a branch that is not taken, an inlined helper
that is never reached -- can slow down a loop somewhere else in the same
function. This guide has several examples.

### Helper calls: `__imulu`, `__ishl`, `__setflag`

The eZ80 has no multiply wider than 8 bits, no divide, no barrel shifter, and
an 8-bit ALU. When C asks for something the chip cannot do in one instruction,
the compiler emits a **call to a library helper** whose name starts with `__`:

| in the assembly | what the C looked like |
|---|---|
| `call __imulu` | a multiply -- including an array subscript |
| `call __ishl`, `call __bshl` | a shift |
| `call __iand`, `call __ior` | a bitwise operation on a 24-bit value |
| `call __idivu`, `call __irems` | `/` or `%` |
| `call pe, __setflag` | repairing the flags after a signed comparison |
| `call __l...` | anything on a 32-bit type |

Section 1a is about finding and removing these. `__frameset` is the exception:
it is the frame prologue, not an arithmetic operation.

### Inlining and outlining

**Inlining** is the compiler copying a function's body into its caller instead
of calling it. **Outlining** is the opposite: deciding to emit the function
once and call it. `static inline` is a *request*; the compiler decides, and
section 2a is about what it decides and when that hurts.

### Tail call

A call whose result is returned immediately -- `return f(x);` -- so nothing in
the caller has to survive it. The caller's registers do not need saving and its
frame can be reused, which makes a tail call much cheaper than a call whose
result is used afterwards.

### The `-Oz` and `-S` flags

`-Oz` is "optimise for size", which is the level agondev builds at and the one
every measurement here was taken at. `-S` makes the compiler write assembly
instead of an object file, which is how you look at any of the above:

```
    ez80-none-elf-clang -mllvm -z80-gas-style -Oz -S file.c -o file.s
```

---

## 1. Choosing the Right Data Sizes (8 vs. 16 vs. 24-bit)

Data size selection is the single most critical factor when writing efficient C for the eZ80. Choosing the wrong width introduces massive instruction bloat.

### 24-Bit Integers (`int` or `int24_t`) -- *Fastest for Pointers & Math*
* **The Architecture:** In ADL mode, native registers (`HL`, `DE`, `BC`) expand to 24 bits. 
* **Optimization Benefit:** Operations on 24-bit integers map natively to CPU instructions. Always use 24-bit types for pointers, array indexing, and general math. 
* **The Danger of 16-Bit:** Using a 16-bit integer for array indexing forces the compiler to generate extra instructions to sign-extend or zero-extend the variable to 24 bits before it can compute a memory address.

### 8-Bit Integers (`char` or `uint8_t`) -- *Fastest for Counters & Flags*
* **The Architecture:** The eZ80 remains an 8-bit chip at its core.
* **Optimization Benefit:** Operations on `uint8_t` variables are extremely fast because they directly map to 8-bit registers like `A`, `B`, or `C`. Use 8-bit integers for any local counter or loop variable that will never exceed 255.

### 16-Bit Integers (`short` or `int16_t`) -- *The Worst Performer in ADL Mode*
* **The Architecture:** In 24-bit ADL mode, 16-bit arithmetic is awkward. The architecture lacks dedicated 16-bit truncation logic within its 24-bit mathematical paths.
* **Optimization Penalty:** If you perform calculations using 16-bit types, the compiler must emit extra operations to mask out or handle overflows in the upper 8 bits of the 24-bit register (`HLU`, `DEU`, etc.). **Avoid 16-bit integers** unless absolutely required for an external file format or specific hardware register constraint.

| Data Type | Width (ADL Mode) | Performance Profile | Primary Use Case |
| :--- | :--- | :--- | :--- |
| `int8_t` / `uint8_t` | 8-bit | **Excellent** | Loop counters, state flags, small buffers |
| `int16_t` / `uint16_t` | 16-bit | **Poor** (unverified) | Avoid (believed to cause masking/extension overhead) |
| `int24_t` / `uint24_t` / `int` | 24-bit | **Excellent** | Memory pointers, array indexing, general math |

### Narrowing pays only if the operations stay narrow -- *measured*

Choosing a narrower type for a field is not on its own an optimisation. What
decides it is the width of **everything that touches the field**: if a narrowed
value is used in an expression with a wider one, C promotes it back, and the
conversion is paid at every use. That can cost more than the wider field ever
did.

Measured in zap, on an Agon, narrowing 32-bit fields to the native 24-bit word:

| what was narrowed | what reads it | result |
| :--- | :--- | :--- |
| `operand.imm` | shifts and masks that stay 24-bit | **−0.8% synth** |
| `operand.reg` alone | compared against a 32-bit table field, so widens back | **+2.4% synth** |
| `operand.reg` and the table field together | nothing widens, but a shared struct is repacked | +1.0% synth |

The same idea, applied three ways, gains 0.8% or costs 2.4% depending only on
what the field is used *with*. The middle row is the trap: it is the change that
looks most obviously correct in isolation, and it is the worst of the three.

Two rules follow:

* **Narrow a field only when every operation on it narrows with it.** Half a
  conversion is worse than none.
* **Beware narrowing a field in a struct other code shares.** Repacking moves
  every field after it, and that cost lands on code that has nothing to do with
  the change.

---

## 1a. The Main Hazard: C That Compiles to Calls, Not Instructions

**This is the single most useful thing in this document.** On a 24-bit machine
with an 8-bit ALU, no barrel shifter and no multiplier wider than `MLT`, a
great deal of ordinary C has no instruction to compile to. The compiler emits a
call to a library helper instead. Nothing in the source suggests it happened,
the code reads as arithmetic, and on the host it *is* arithmetic — so it is
invisible to a host profile and to review.

Every large win in this work was one of these, found by reading generated
assembly rather than by thinking harder about the C.

### The offenders

| What you write | What you get | Why |
|---|---|---|
| `x << 3`, `x << 4`, `x >> 4` | `call __bshl` / `__bshru` | No barrel shifter: a shift is a loop over the bits |
| `x << 8`, `x << 16` | `call __ishl` | Even byte boundaries — the compiler does **not** turn a *left* shift into a byte move |
| `(uint8_t)(v >> 16)` where `v` is in a register | `call __ishru` | Only `>> 8` gets the byte trick (`ld a, h`); HL's upper byte is not addressable |
| `a & b` on a 24-bit value | `call __iand` | `AND` is an 8-bit instruction |
| `a \| b` on a 24-bit value | `call __ior` | Same |
| `x != 0` on a 24-bit value | `call __lcmpzero` | Same |
| `arr[i]` where `sizeof(*arr) != 1` | `call __imulu` | The subscript is `i * size`, and `MLT` is 8-bit |
| `p += n` on a pointer to a struct | `call __imulu` | Same, and easy to miss — it looks like pointer arithmetic |
| anything on `uint32_t`/`long` | `call __l*` | Twice the machine's width |
| `x / y`, `x % y` | `call __idivu` / `__irems` | No divide instruction at all |

### The fixes, in order of how often they apply

1. **Precompute into a table.** `shl3[i & 7]` instead of `i << 3`: an indexed
   load from ≤256 bytes is one instruction. Repeated addition does *not* work —
   the compiler canonicalises `x+x+x+x` back into a shift.
2. **Split wide values into bytes, at the point they are created.** A 24-bit
   mask that is only ever masked or tested against zero should be three
   `uint8_t`. Do the split where the value is born, not where it is used, or
   every user pays it.
3. **Keep constants constant.** A constant shift folds only while the value is
   still a constant. After a `switch` joins, `bit >> 16` is a runtime shift and
   therefore a call — sink the stores into the arms instead.
4. **Hand out pointers that already exist.** Returning a pointer *from a data
   structure* removes the subscript's multiply. But see the trap below.
5. **Read bytes out of memory, not out of a local.** `(uint8_t)(op->imm >> 16)`
   is an indexed load; hoist `op->imm` into a local first and it becomes a call.

### The trap in fix 4

"Hand out a pointer" does not mean "invent objects to point at". Returning
pointers to twenty-eight `static const` descriptors removed a `__ishru` and
still lost 0.4%, because the compiler hoisted their addresses into the frame
prologue — `ld de, _rd_a; ld (ix - 17), de` — paid on every call, and the frame
grew past a size that mattered. The same call removed by sinking the stores
into the switch arms, creating nothing new, won 4.9%. **Ask where the pointer
comes from.**

### How to find them

    ez80-none-elf-clang ... -S file.c -o file.s
    grep -o 'call[ \t]*__[a-z0-9_]*' file.s | sort | uniq -c

Anything other than `__frameset` is an operation the chip does not have. Do
this before optimising anything, and again after — several of these appeared
*because* of a change that looked like an improvement.

---

## 2. Core Architecture Rules

### Do NOT Prefer Global/Static Over Stack Locals -- Stack Access Is Faster
* **The half-truth:** It is true that the eZ80 has no `Stack Pointer + Offset` addressing mode for general registers. `ld hl, (sp+3)` is not a valid instruction; only `ld hl, (sp)` and `add hl, sp` exist, and `lea` works on `IX`/`IY` only. So a compiler does use an index register as a frame pointer for locals.
* **Why that does not make statics faster:** the frame pointer is set up *once per function*, and every access after that is **cheaper** than direct addressing. From the Attributes tables in Zilog UM0077, in ADL mode:

| Access | Instruction | Cycles | Bytes |
| :--- | :--- | ---: | ---: |
| static / global | `LD HL, (Mmn)` | **7** | 4 |
| stack local via IX | `LD HL, (IX+d)` | **6** | 3 |
| stack local via IY | `LD HL, (IY+d)` | **5** | 3 |
| static / global | `LD rr, (Mmn)` | **8** | 5 |
| stack local via IX | `LD rr, (IX+d)` | **6** | 3 |

* **Measured:** making a hot 2 KB parser struct `static` instead of a stack local in a real assembler changed its runtime by nothing at all -- 87.90 s in both cases on an emulated Agon, deterministic across three runs. It also costs re-entrancy, which matters if the code is a library.
* **The real rule:** leave locals on the stack. What *does* matter is how *large* they are: a 41 KB struct on the stack is 41 KB the program touches. Shrinking that same struct to 2.2 KB was free and saved the memory.

**But a struct reached through a pointer parameter is a different question.**
Passing `state*` to everything means every field access first loads that
pointer out of the frame -- and ties up an index register that a loop wanted.
Making the same object file-scope, so its fields are addressed absolutely, was
worth **4.9%** in zap. The comparison that matters is not "local versus
static"; it is "one absolute address versus a base register you have to fetch
and keep".

### Structure Loops to Count Down to Zero
* **The Problem:** Compiling a standard incrementing loop (e.g., `for (uint8_t i = 0; i < 10; i++)`) forces the compiler to run an explicit comparison instruction (`cp 10`) on every single iteration.
* **The Fix:** Structure loops to count down to zero (e.g., `for (uint8_t i = 10; i > 0; i--)` or `while(--i)`). The eZ80 hardware natively tracks when a value decrements to zero via the CPU's Zero Flag, completely eliminating the comparison step.

### Leverage the 8-bit Hardware Multiplier (`MLT`)
* **The Architecture:** Unlike the original Z80, the eZ80 features a built-in hardware multiplier instruction (`MLT`). It multiplies two 8-bit registers and returns a 16-bit result (e.g., `HL = H * L`).
* **The Fix:** Keep your multiplication factors strictly to 8 bits (`uint8_t`). If you multiply two 24-bit variables, the compiler cannot use the `MLT` instruction directly and falls back to a slow, multi-step Software Math Library routine.

### Pass Arguments via Registers
* **The Strategy:** Stack access is slow. Keep performance-critical inner functions limited to 2 or 3 arguments so they stay entirely inside registers (`HL`, `DE`, `BC`) rather than spilling onto the stack. Check your specific compiler's calling conventions (e.g., CE Dev LLVM or Zilog ZCC) to optimize function signatures.

### Avoid Passing Structures by Value
* **The Strategy:** Never pass structures to functions by value. Doing so triggers an expensive block memory copy (`LDIR`) onto the stack. Always pass structures via a pointer.

---

### Branchless is not unconditional -- *measured*

The usual advice for a machine with no branch predictor is to prefer
data-driven code -- a table lookup, arithmetic on a flag -- over a chain of
`if`/`else`. That holds for a branch that **selects a value**, where both
alternatives cost about the same and the branch buys nothing.

It does not hold for a branch that **skips work**. zap's instruction-row
selection evaluated every term of its test so the whole thing could be one
branch. Rejecting a candidate on the cheapest term first, and only then paying
for the expensive ones, was worth **6.4% on zap's instruction-dense
benchmark**, and 23.2% on the stripped version it was first measured on -- on
a chip with no branch predictor.

The distinction is whether the branch avoids computation. If it does, take it.

### One index register is the budget inside a loop -- *measured*

`ix` is the frame pointer and `iy` is everything else, so a loop that walks a
struct has exactly one register to hold the pointer it is walking. Any
expression that needs a *second* computed address inside that loop evicts the
first one to the frame and reloads it, once per use.

zap's row test reads three adjacent bytes of a row and ANDed each against the
matching byte of the operand. The three bytes are the three planes of a
register mask, only one of which can be set -- so an obvious improvement is to
store which plane, and read just that one: `(&ri->a0)[plane]`. Three loads and
three ANDs become one load and one AND.

It cost **6.2%**. The variable index has to be added to the row pointer, which
means the row pointer has to be in `hl` to take an `add`, which means it is not
in `iy` any more, so every other field of the row -- and the row pointer itself
on the way round the loop -- goes through the frame:

```
ld  iy, (ix - 42)     ; reload the row pointer
ld  bc, (ix - 48)     ; the plane index
add iy, bc
ld  a, (iy + 2)
ld  iy, (ix - 42)     ; and put it back for the next field
```

Three loads and three ANDs, each `ld a, (iy+n); and a, (ix+m)`, is two
instructions per plane with no address arithmetic at all. Fewer operations lost
to more addressing.

**The rule:** inside a loop over a structure, prefer constant offsets from one
index register over any computed address, even when the computed address
replaces several constant ones. Count the reloads, not the operations.

---

## 2a. Inlining Is a Request, and Function Shape Is a Cost

`static inline` is advice to the compiler, not an instruction to it, and on a
machine where a call is expensive the difference between advice taken and
advice ignored is large. Four separate lessons here, all measured on a real
assembler, all invisible in the C.

### One cold caller de-inlines a hot helper for everybody

A `static inline` helper called from one hot place is inlined. Add a second
caller anywhere -- a cold one, on the command-line path, that runs once per
process -- and the compiler may decide to emit the function out of line and
turn *every* call site into a real call, including the one on the hot path.

This happened five times in one program, each time to a different helper:

| helper | the cold caller that did it | cost when outlined |
|---|---|---|
| case-insensitive compare | option parsing at startup | 5.5% of runtime |
| mnemonic lookup | the mode-suffix reader | isa_degenerate 4.86s → 5.16s |
| the row matcher | the suffixed-instruction path | 4.86s → 5.80s |
| an argument parser | a second directive | measurable on every line |
| the forward-reference result | one directive | a call with three out-parameters per reference |

**The fix is `__attribute__((always_inline))`** on the helpers that must stay
inlined, and a rule of thumb: whenever you add a caller to an existing
`static inline`, check that it is still inlined at the sites that matter.

    grep -c '^_helper_name:' file.s     # 0 = inlined everywhere, 1 = outlined

### A cold function with a big buffer must not be inlined into a hot one

The reverse case. A function that is only reached when something unusual
happens, but that declares a large local buffer, will contribute that buffer to
the frame of whatever it is inlined into:

    list_args()  -- 176-byte line buffer, runs only when a listing is on
    inlined into macro_expand()  -- frame 73 bytes → 267 bytes

Every macro expansion in every program then paid for a listing nobody asked
for, because a 267-byte frame is well past the 128 bytes an `ix` displacement
reaches. `noinline` on the cold function put the frame back to 79 bytes.

**Ask what a cold function contributes to a hot frame**, not just how often it
runs.

### Tail calls keep a frame off the common path

A function that falls through into more work has to keep everything alive
across the call. A function that *returns* the call's result does not:

```c
    /* costs the common path: everything live across the call */
    insn = try_something(s, n, p, e, &stop);
    if (insn != NULL) { ...carry on with operands... }

    /* costs the common path nothing */
    return try_something(s, n, p, e, stop);
```

In zap's line assembler, the directives, the mode suffixes and the
three-operand forms are all reached by a tail call from the point where the
mnemonic lookup failed. Written as a fall-through instead, the same code cost
**6% on every benchmark, including the ones with no directive in them.**

### Code that is merely *there* costs

Two hundred instructions of argument parsing sitting in front of the line loop
-- never executed on the benchmark, since the option was not given -- cost
**5.3%**, by moving the loop's register allocation. `noinline` on the argument
parser recovered it.

The rule this generalises to: on a machine with one usable index register
inside a loop, anything that lengthens the function containing that loop can
evict the pointer it walks. Measure the loop, not the feature.

### Calls versus frames: sometimes the call is cheaper

A range check on every immediate was written two ways. Inside a helper the
compiler made a real function of, it cost a call per immediate. Hoisted to the
call site as a macro so no call happens, it grew `assemble_line`'s frame from
108 bytes to 111 -- and *that* was slower.

**Near the 128-byte edge, three bytes of frame can cost more than a call per
iteration.** Which way round it falls has to be measured; the point is that
both directions are real.

---

## 3. Advanced Memory & Mathematical Optimizations

### Master `const` and Memory Segments (RAM vs. Flash)
* **The Strategy:** Mark all look-up tables, static strings, and fixed data arrays as `const`. 
* **Why it matters:** If constant data is not explicitly declared as such, the compiler may copy it to RAM during startup, wasting fast scratchpad space and bloating your initialization routine. Ensure your linker script routes `.text` (code) and `.rodata` (constants) to your hardware's fastest zero-wait-state memory banks.

### Replace Bit-Shifting with Byte-Swapping
* **The Problem:** The eZ80 lacks a barrel shifter. Shifting a 24-bit integer by an arbitrary amount requires looping a 1-bit shift instruction multiple times. Writing `uint24_t x = y >> 8;` forces an explicit 8-iteration shift loop.
* **The Fix:** Align your bitwise shifts to multiples of 8 bits whenever possible, and where you cannot, use a lookup table.

**Measured, on agondev's clang at `-Oz`.** Every shift by a constant that is not a byte boundary is a call -- `(v & 7) << 3` compiles to `ld b, 3; call __bshl`. Writing it as repeated addition does not help: `((x+x)+x)+x` is canonicalised straight back into a shift, and so is `* 8`. A lookup table is the way out, because an indexed load from 256 bytes or fewer is one instruction:

```c
static const uint8_t shl3[8] = { 0, 8, 16, 24, 32, 40, 48, 56 };
opcode |= shl3[index & 7];          /* not opcode |= index << 3 */
```

Byte boundaries are cheaper but not uniformly free, and the difference is *where the value lives*:

| expression | value in a register | value still in memory |
|---|---|---|
| `(uint8_t) v` | `ld a, l` | `ld a, (iy+n)` |
| `(uint8_t)(v >> 8)` | `ld a, h` | `ld a, (iy+n+1)` |
| `(uint8_t)(v >> 16)` | `ld c, 16; call __ishru` | `ld a, (iy+n+2)` |

HL's upper byte is not directly addressable, so the compiler has the byte trick at `>> 8` and loses it at `>> 16`. The practical consequence is the opposite of the usual advice: **do not hoist a value into a local to take bytes out of it.** Reading the struct field afresh for each byte is what makes all three an indexed load. This was worth 1.5% of zap's whole run time on one function.

### Exploit Block Memory Instructions (`LDIR` / `CPIR`)
* **The Strategy:** Do not write manual `for` loops to copy arrays or clear memory buffers. Always rely on standard C library string and memory utilities: `memcpy()`, `memmove()`, and `memset()`.
* **Why it works:** Modern eZ80 C compilers heavily optimize standard library memory operations directly into the CPU's hardware-accelerated block-transfer (`LDIR`, `LDDR`) or block-search (`CPIR`, `CPDR`) assembly loops.

### Use Power-of-Two Array Sizes to Avoid Division
* **The Problem:** The eZ80 completely lacks a hardware division instruction. Modulo (`%`) and division (`/`) operators trigger slow software math library subroutines.
* **The Fix:** Keep your array dimensions, matrix widths, and circular buffer sizes strictly mapped to powers of two (e.g., 16, 64, 256). This enables the compiler to optimize the operation, swapping out division for an instantaneous bitwise AND operation (`index & 255`).

### Inline Small, Critical Functions
* **The Problem:** Function calls introduce a heavy penalty because the CPU must push the 24-bit program counter onto the stack, jump, and pop it back off upon returning.
* **The Fix:** Use the `inline` or `static inline` keyword for small, frequently called helper functions (such as pixel plotting, bit masking, or mathematical macros) inside inner loops. This eliminates the `CALL` and `RET` overhead entirely by embedding the code directly into the instruction flow.
* **The limit:** only while the resulting frame stays under 128 bytes -- see
  the next heading, and section 2a for the two ways inlining gets that wrong.

### Keep Every Stack Frame Under 128 Bytes
* **The Problem:** `ix` displacement is a **signed byte** (see section 0). A function whose frame exceeds 128 bytes cannot reach most of its own locals with `ld a, (ix-9)`, and the compiler falls back to computing the address:

```
    ld   bc, -139
    lea  hl, ix + 0
    add  hl, bc
    ld   hl, (hl)      ; five instructions where there was one
```

  This is paid on **every access** to every local past the boundary, and nothing in the source suggests it is happening.
* **The Fix:** Split the function, or move the locals an inner loop touches into a small helper. Counter-intuitively this can make the program *smaller*: splitting one 149-byte frame into four of 60, 62, 19 and 20 removed 23 escape sequences and cut 77 instructions from the binary, despite adding four call/return pairs. It was worth **7.8%** overall and **28.3%** on the hottest loop.
* **How to check:** compile to assembly and count. `grep -c 'lea.*hl, ix + 0'` finds the escapes; `grep -o 'ld.*hl, -[0-9]*'` after each `__frameset` gives the frame sizes.
* **Where this bites hardest:** aggressive inlining. The four functions above were not written large -- they were separate, and the compiler folded them all into `main`. `-Oz` will happily inline a whole program into one frame and then pay five instructions for every local in it.

---

## 3a. Memory on a Machine With No Virtual Memory

512 KB, no swap, no MMU. Two consequences that do not arise on a desktop.

### A realloc that moves holds both copies

The transient peak decides whether a growth succeeds, not the final size. An
arena of 110 KB growing by eight bytes needs 228 KB at the moment of the copy,
and on a machine with 512 KB that is where an assembler runs out -- two thirds
of the way through a symbol table it would otherwise have finished.

Two fixes, both used in zap:

* **Grow by doubling, not by a fixed step.** Reaching a 197 KB output by 32 KB
  steps takes five growths and the last asks for 192 + 224 = 416 KB. Doubling
  reaches it in two and the last asks for 128 + 256 = 384 KB. The peak goes
  from about twice the final size to about 1.5 times it.
* **For anything that only ever appends, use an arena of blocks that never
  move.** A block is allocated, filled and never resized, so the peak *is* the
  total. Pointers into it stay valid, which is worth an add on every compare
  compared with storing offsets and rebasing.

### Allocation is cheap; touching memory is not

Replacing a 39.7 KB fixed array with per-item allocation was free in time and
saved 95% of the footprint. On this target, trading an allocation for a smaller
resident size is nearly always a good trade -- there is no cache to lose and no
pressure the allocator itself creates.

The corollary is that a 41 KB struct on the stack is 41 KB the program touches.
Shrinking one to 2.2 KB cost nothing and gave the memory back.

### Measure memory the way you measure time

Speed gets measured every round and memory usually does not, which is the wrong
way round here: an assembler that is 5% faster and does not fit is not faster.
An allocation shim that reports the peak, switched on by renaming the
allocators on the compile line, costs nothing when it is off:

    -DZMALLOC -Dmalloc=z_malloc -Dcalloc=z_calloc \
              -Drealloc=z_realloc -Dfree=z_free

---

## 3b. Two Miscompiles to Know About

Both at `-Oz` with agondev's clang, both silent, both correct on the host.

### An unbounded character scan can be compiled rotated

```c
    while (cls(*p)) { p++; }              /* the mistake */
    while (p < e && cls(*p)) { p++; }     /* the fix */
```

Given a sentinel that stops the scan, the first form is safe C. The compiler
may nevertheless produce a loop with the pointer **pre-decremented**, each turn
testing one character *past* it -- so the first character is never examined and
the scan stops one short. `ld a, 0x42` parses as `0x4` and leaves `2` behind.

It does not reduce: the same loop in isolation compiles correctly. Indexing
from a base instead of advancing a pointer fails the same way. It has appeared
five times on five different loops, and once on four loops in a function whose
own source had not changed -- a function was added elsewhere and the register
allocation moved.

**Every character scan carries its bound.** In zap that costs 0.06s of a 5.6s
run, and a test reads the source to check no scan has lost one.

### A backwards trim reads one byte too far

```c
    while (ae > as && is_space_ch(ae[-1])) { ae--; }   /* miscompiled at -Oz */
```

The decrement is committed before the test, so the byte examined is `ae[-2]`. A
single-character macro argument sees the space in front of it, trims itself
away and expands to nothing.

The fix is not to trim backwards at all: track the last non-space *while
scanning forward*, which needs no backward index and is one pass rather than
two.

### What both have in common

The host build is correct in both cases, under every sanitiser, at every
buffer size. Nothing but reading the generated assembly or running on the
target finds them.

    ez80-none-elf-clang ... -S file.c -o file.s
    grep -B2 -A6 'dec iy' file.s      # a rotated scan's signature

---

## 3c. Width and Sign

### A comparison between different widths tests the wrong bytes

An assembler that evaluates expressions in 32 bits on a machine whose `int` is
24 has to be careful at every boundary. Comparing a 32-bit value against a bare
`int` constant compiled to a test of the wrong part of the value -- and refused
an addend of zero, on the target, while the host build was correct.

**Both sides of every comparison must be the same width**, written with typed
constants rather than bare literals:

```c
    #define FIX_ADDEND_MIN ((evalue) -8388608)
    #define FIX_ADDEND_MAX ((evalue)  8388607)
```

Deriving such a limit from `sizeof(int)` is worse than writing it out: it makes
the program accept on one machine and refuse on the other.

### A signed compare is a call

Two signed `int`s compared with `<` cannot be done in one subtract, so the
compiler emits `call pe, __setflag` to repair the flags on overflow. Inside a
loop -- comparing a length, bounding a table walk, reserving output space --
that is a helper call per iteration.

Making three loop counters unsigned was worth 1.4% of a whole run. Where a
quantity cannot be negative, say so in the type.

    grep -c 'call[ \t]*pe, __setflag' file.s

### An out-parameter puts a value in memory

A helper that advances the caller's cursor through `const char** pp` forces
that cursor into the frame: its address has been taken, so it cannot live in a
register. Returning the new position instead, and letting the caller assign it,
was worth 2.4% on the data-directive path even with the helper inlined.

The same effect shows up wherever `&local` is passed anywhere -- including to a
function that is inlined afterwards.

---

## 4. Provenance -- What Here Is Verified

Not every claim in this document is equally supported. Treat them accordingly.

### Verified against Zilog UM0077 (the instruction Attributes tables, from p. 79)
* Stack-local access is **faster** than static/global access (6 vs 7 cycles for `HL`, 6 vs 8 for `BC`/`DE`). The "prefer static" advice above was wrong and has been corrected.
* There is **no division instruction** in the eZ80 instruction set, so `/` and `%` do call software routines. Power-of-two sizes are worth it.
* `MLT` is an 8x8 multiply returning 16 bits "regardless of the ADL mode", so keeping multiplication factors in `uint8_t` is correct.
* `LDIR` and `CPIR` exist, so `memcpy`/`memmove`/`memset` do map to block instructions.

### Verified by measurement on an emulated Agon
Each figure below is that program's own reported time, with the emulator's CPU
limited to the real 18.432 MHz clock (do **not** use `-u`: with the CPU
unthrottled the guest's `clock()` measures how fast the *host* emulated the
work). Readings are deterministic to the centisecond.

* **Shrinking a hot struct pays.** A 17-byte token to 13 bytes: **-3.4%**.
* **Inlining small hot functions pays.** Removing one call per token: **-2.9%**.
* **`memcpy` over a hand-written byte loop pays, a lot.** Block-copying `.incbin` data instead of a byte at a time: **-22.8%**.
* **`memset` over a hand-written byte loop, likewise.** The fill an `ORG`
  writes when it skips forward, a byte at a time against one `memset`: a
  source with 96 KB of gap went **0.56 s to 0.10 s**. Worth noting that this
  rule was already written down here and the loop was there anyway -- a
  four-line loop filling a buffer does not look like a hot path until
  something asks it for 96 KB.
* **Not passing structs by value pays** -- the token result above is exactly this effect.
* **Allocation is cheap; touching memory is not.** Replacing a 39.7 KB fixed
  array with per-item allocation was free, and cost 95% of the struct's size.
  Trading an allocation for a smaller footprint is a good trade here.
* **Constant shifts that are not byte boundaries are calls.** `<< 3` and `<< 4`
  replaced by 8- and 16-entry lookup tables: **-1.6%**. Repeated addition is
  not a workaround; the compiler canonicalises it back into a shift.
* **Byte extraction is free from memory, not from a register.**
  `(uint8_t)(v >> 16)` is one indexed load when `v` is a struct field and a
  call to `__ishru` when it has been hoisted into a local: **-1.5%** for not
  hoisting.
* **Frames over 128 bytes cost five instructions per local access.** Splitting
  one 149-byte frame into four small ones: **-7.8%** overall, **-28.3%** on
  the loop that paid it most, and 77 fewer instructions in the binary.
* **One record per row beats parallel arrays, for a loop that reads several
  fields of the same row.** Eight `uint8_t` arrays indexed by `r` were
  **+8.5%**; the same eight bytes in one struct walked by a pointer were
  **-10.1%** overall and **-35.0%** on the row-heavy case. Indexed addressing
  off `iy` amortises the base-pointer arithmetic that each separate array
  repeats.
* **24-bit AND is a call.** `AND` is an 8-bit instruction, so `regset & reg` on
  a `uint24_t` compiles to `call __iand`, and indexing an array of them costs
  `r * 3`, a `call __imulu`. Part of the row-record figure above.
* **A stored pointer beats a computed one -- but not a materialised one.**
  Handing out a pointer that already exists in a data structure removes the
  multiply a subscript needs, and was worth 20.5% on one lookup. Returning
  pointers to many *distinct compile-time* objects is the opposite: with
  twenty-eight of them in switch arms the compiler hoisted their addresses into
  the frame prologue, and the change lost 0.4% despite also removing a call to
  `__ishru`. The question to ask is where the pointer comes from, not whether
  it is a pointer.
* **Removing a loop bound can change what the loop computes.** Rewriting
  `while (p < e && cls(*p)) p++;` as `while (cls(*p)) p++;` -- safe C, given a
  sentinel that stops the scan -- produced a loop with the pointer
  pre-decremented and each iteration testing one character *past* it, so the
  first character was never examined and the scan stopped one short. It does
  not reduce: the same loop alone compiles correctly, and indexing from a base
  instead of advancing a pointer fails the same way. The host is no help --
  every host test passed, at four buffer sizes, under ASan. **Read the
  generated assembly for any scan you unbound, and test it on hardware.**
* **The emulator is deterministic to about 0.25%.** Three interleaved repeats
  of two binaries gave 8.24/8.26/8.24 against 8.28/8.28/8.28. Do not claim a
  change under half a percent from a single run, and do not dismiss a
  consistent 0.4% as noise.
* **A `static inline` helper stops being inlined when a second caller appears.**
  Five helpers in one program, each de-inlined by one cold caller; the worst
  was 5.5% of runtime, another took a benchmark from 4.86s to 5.80s.
  `always_inline` on the ones that matter.
* **A cold function's local buffer joins the frame it is inlined into.** A
  176-byte listing buffer inlined into the macro expander took its frame from
  73 bytes to 267 -- paid by every expansion in every program, listing or not.
* **A tail call keeps a frame off the common path.** Reaching the directives,
  the mode suffixes and the three-operand forms by tail call rather than
  falling through was worth **6% on every benchmark**, including those with no
  directive in them.
* **A file-scope state object beats a pointer parameter: 4.9%.** Absolute
  addressing, and one fewer index register tied up.
* **Unsigned loop counters are worth real time.** Three of them, replacing
  signed compares that each carried a `call pe, __setflag`: **1.4%**.
* **An out-parameter costs even when the helper is inlined.** A helper that
  advanced the caller's cursor through `const char**` forced that cursor into
  memory: **2.4%** on the path that used it. Return the position instead.
* **`-O2`, `-O3` and `-Ofast` were all slower than `-Oz`** on this program. On
  a machine with no cache, the size of the code is part of its speed; do not
  assume a higher optimisation level is an improvement without measuring it.
* **A check asked of every input is the expensive kind of diagnostic.** A
  truncation check on every value written cost **2.1% of a real program and
  6.7% of a synthetic one**, where every other diagnostic in the same program
  -- all of which do their work only after something has already failed --
  measures at nothing. Work done on failure is free; questions asked of
  everything are not.
* **A backwards trim is miscompiled at -Oz.** `while (ae > as && sp(ae[-1])) ae--;`
  reads `ae[-2]`. See section 3b.
* **The host understates target gains by about 2.5x.** The same 135 sources
  measured 1.36x on the host and 3.20x on the Agon.

### Contradicted by measurement
* **"Data-driven beats branching" is too simple**, taking that as the general
  advice it usually is rather than as a claim made anywhere here. Replacing a
  chain of ~8 failing character comparisons with two lookups in a 256-byte
  table was **0.3% slower**. Replacing short-circuit `&&`/`||` with bitwise operators on
  values already in registers was **0.8% faster**.
* The rule that fits both: **not-taken branches are cheap, memory accesses are
  not.** Replace a branch with register arithmetic and you win; replace it with
  a table lookup and you lose.

### Plausible but unverified
* The claim that 16-bit types are the *worst* performer. This is about
  compiler-generated masking rather than instruction timing, so the ISA tables
  cannot confirm it.
* The three-stage pipeline and its 1-2 cycle taken-branch penalty. Not in the
  instruction tables; the branching results above are consistent with it but do
  not establish it.

### A note on measuring at all
The host is **not** a proxy for this target. It is biased, not merely noisy, and
in the direction that flatters the work -- on a real assembler, host instruction
counts overstated the gain from three lexer changes by about 3x, and called the
token shrink a *regression* when it was the largest win of the set. Measure on
the target: section 5 says how.

### A constant shift is a call, and you cannot write your way out -- *measured*

Section 1a lists `__ishl` among the offenders for variable shifts. It is worth
saying plainly that a **constant** shift is one too, and that the obvious
workarounds do not work.

`x * 4` on a 24-bit value compiles to `ld c, 2; call __ishl`. So does `x << 2`.
So does `x += x; x += x;` -- LLVM canonicalises the pair of adds back into a
shift before the backend ever sees them, and the backend lowers the shift to a
helper. There is no spelling of "double this twice" that survives.

What this means in practice is that **turning an array index into an address
always costs one helper call**, and the only choice is which one:

| element size | what you get |
| :--- | :--- |
| 3 bytes (a bare pointer) | `call __imulu` |
| 4 bytes (a pointer plus a pad) | `call __ishl` |
| 1 byte (an index, not a pointer) | nothing -- but converting the index back to a pointer costs a scale |

Padding a lookup table's entries to a power of two to buy the cheaper helper
was worth **1.8%** in a real assembler's mnemonic lookup, once per line of
source. Going further is not possible: storing byte indices removes the scale
from the table read and puts an identical one on the array it indexes into.

Note the portability trap. `(uint8_t*) table + b + b + b` does remove the call,
and is wrong anywhere a pointer is not three bytes -- which includes the host
the unit tests run on. Change the element size, not the arithmetic.

---

## 5. How to Measure on This Target

Every figure in this document came out of the method below. It is worth as much
as the findings.

### The host is not a proxy, and it is biased rather than noisy

The same 135 corpus sources, assembled by the same two assemblers, measured
both ways:

| | geometric mean speedup |
|---|---|
| on the host, x86-64 | **1.36x** |
| on the Agon, 18.432 MHz | **3.20x** |

Two and a half times out, in the direction that would have made almost every
eZ80-specific change look not worth doing. Host instruction counts have also
overstated a lexer change by 3x and called the largest win of a set a
regression.

Use the host for correctness. Measure speed on the target.

### Read the program's own clock, and do not unthrottle the emulator

fab-agon-emulator with `-u` runs the guest as fast as the host can emulate it,
which decouples the guest's `clock()` from the work it does; the number stops
meaning anything. Run it at the real 18.432 MHz and read the line the program
prints for itself.

### The clock counts hundredths, so repeat and sum

A twenty-line source assembles in 2 to 30 ms, which reads as `0.00` or `0.01`.
Assemble it eight or twelve times and sum the reported times: the tick boundary
falls in a different place on each run, so the quantisation averages out
instead of accumulating. Eight runs of a 25 ms assembly sum to 0.20 give or
take a hundredth -- 5% rather than 50%.

### One change, one measurement, nothing else running

* The emulator is deterministic to about 0.25%: three interleaved repeats of
  two binaries gave 8.24/8.26/8.24 against 8.28/8.28/8.28. Do not claim a
  change under half a percent from a single run, and do not dismiss a
  consistent 0.4% as noise.
* Two emulators on one host time each other's work. A stale benchmark left
  running invalidated a whole afternoon's numbers in this project.
* Do not edit the source while a benchmark is in flight. The binary that gets
  timed is the one that was built, and it is easy to lose track of which.

### Pricing a piece of code without instrumenting it

Instrumentation changes what you are measuring: a second call to a function
inlined into a 3,500-line hot function makes the compiler outline it, and the
difference then includes the outlining.

Two techniques that do not have that problem:

**Marginal pricing by duplication.** Build the table with every entry
duplicated. The walk over it does twice the work, a match is still found at the
first copy, and **the output is byte-identical** -- which is what says the
measurement is valid. The extra time is that walk's cost:

    make EXTRA_CFLAGS=-DDUP_ROW      # the register test in the row matcher
    make EXTRA_CFLAGS=-DDUP_BUCKET   # the mnemonic bucket chain

**Input-differential attribution.** One binary, several inputs of identical
size that differ in exactly one feature -- the same source with and without
macros, with and without conditionals, with and without EQU. The difference in
time is that feature's cost, and no build flag is involved.

The technique to avoid is a *staged truncation build* -- `#ifdef` that stops
the program part way through -- because each stage is a different program with
a different register allocation, not the same program with a piece removed. Two
changes made on that evidence in this project were both slower.

### Read the generated assembly

The single most useful habit. Before optimising, and again after:

    ez80-none-elf-clang -mllvm -z80-gas-style -Oz -S file.c -o file.s

    grep -o 'call[ \t]*__[a-z0-9_]*' file.s | sort | uniq -c   # helper calls
    grep -c 'call[ \t]*pe, __setflag' file.s                   # signed compares
    grep -A2 '__frameset' file.s                               # frame sizes
    grep -c 'lea.*hl, ix + 0' file.s                           # frame escapes
    grep -c '^_helper:' file.s                                 # inlined or not

Several of the regressions in this project appeared *because* of a change that
looked like an improvement, and all of them were visible here first.
