# Advanced eZ80 C Optimization Guide

An authoritative reference for optimizing C code targeting the Zilog eZ80 microcontroller architecture, specifically tailored for execution in **ADL (24-bit) mode**.

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

Every large win in the dzap work was one of these, found by reading generated
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

Section 2's advice that data-driven code beats chains of `if`/`else` holds for a
branch that **selects a value**, where both alternatives cost about the same and
the branch buys nothing.

It does not hold for a branch that **skips work**. zap's instruction-row
selection evaluated every term of its test so the whole thing could be one
branch. Rejecting a candidate on the cheapest term first, and only then paying
for the expensive ones, was worth **23.2% in dzap and 6.4% on zap's
instruction-dense benchmark** -- on a chip with no branch predictor.

The distinction is whether the branch avoids computation. If it does, take it.

### One index register is the budget inside a loop -- *measured*

`ix` is the frame pointer and `iy` is everything else, so a loop that walks a
struct has exactly one register to hold the pointer it is walking. Any
expression that needs a *second* computed address inside that loop evicts the
first one to the frame and reloads it, once per use.

dzap's row test read three adjacent bytes of a row and ANDed each against the
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

HL's upper byte is not directly addressable, so the compiler has the byte trick at `>> 8` and loses it at `>> 16`. The practical consequence is the opposite of the usual advice: **do not hoist a value into a local to take bytes out of it.** Reading the struct field afresh for each byte is what makes all three an indexed load. This was worth 1.5% of dzap's whole run time on one function.

### Exploit Block Memory Instructions (`LDIR` / `CPIR`)
* **The Strategy:** Do not write manual `for` loops to copy arrays or clear memory buffers. Always rely on standard C library string and memory utilities: `memcpy()`, `memmove()`, and `memset()`.
* **Why it works:** Modern eZ80 C compilers heavily optimize standard library memory operations directly into the CPU's hardware-accelerated block-transfer (`LDIR`, `LDDR`) or block-search (`CPIR`, `CPDR`) assembly loops.

### Use Power-of-Two Array Sizes to Avoid Division
* **The Problem:** The eZ80 completely lacks a hardware division instruction. Modulo (`%`) and division (`/`) operators trigger slow software math library subroutines.
* **The Fix:** Keep your array dimensions, matrix widths, and circular buffer sizes strictly mapped to powers of two (e.g., 16, 64, 256). This enables the compiler to optimize the operation, swapping out division for an instantaneous bitwise AND operation (`index & 255`).

### Inline Small, Critical Functions
* **The Problem:** Function calls introduce a heavy penalty because the CPU must push the 24-bit program counter onto the stack, jump, and pop it back off upon returning.
* **The Fix:** Use the `inline` or `static inline` keyword for small, frequently called helper functions (such as pixel plotting, bit masking, or mathematical macros) inside inner loops. This eliminates the `CALL` and `RET` overhead entirely by embedding the code directly into the instruction flow.
* **The limit:** only while the resulting frame stays under 128 bytes. See below.

### Keep Every Stack Frame Under 128 Bytes
* **The Problem:** `ix` displacement is a **signed byte**. A function whose frame exceeds 128 bytes cannot reach most of its own locals with `ld a, (ix-9)`, and the compiler falls back to computing the address:

```
    ld   bc, -139
    lea  hl, ix + 0
    add  hl, bc
    ld   hl, (hl)      ; five instructions where there was one
```

  This is paid on **every access** to every local past the boundary, and nothing in the source suggests it is happening.
* **The Fix:** Split the function, or move the locals an inner loop touches into a small helper. Counter-intuitively this can make the program *smaller*: in dzap, splitting one 149-byte frame into four of 60, 62, 19 and 20 removed 23 escape sequences and cut 77 instructions from the binary, despite adding four call/return pairs. It was worth **7.8%** overall and **28.3%** on the hottest loop.
* **How to check:** compile to assembly and count. `grep -c 'lea.*hl, ix + 0'` finds the escapes; `grep -o 'ld.*hl, -[0-9]*'` after each `__frameset` gives the frame sizes.
* **Where this bites hardest:** aggressive inlining. The four functions above were not written large -- they were separate, and the compiler folded them all into `main`. `-Oz` will happily inline a whole program into one frame and then pay five instructions for every local in it.

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

### Contradicted by measurement
* **"Data-driven beats branching" is too simple.** Replacing a chain of ~8
  failing character comparisons with two lookups in a 256-byte table was
  **0.3% slower**. Replacing short-circuit `&&`/`||` with bitwise operators on
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
the target.
