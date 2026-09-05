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

## 3. Advanced Memory & Mathematical Optimizations

### Master `const` and Memory Segments (RAM vs. Flash)
* **The Strategy:** Mark all look-up tables, static strings, and fixed data arrays as `const`. 
* **Why it matters:** If constant data is not explicitly declared as such, the compiler may copy it to RAM during startup, wasting fast scratchpad space and bloating your initialization routine. Ensure your linker script routes `.text` (code) and `.rodata` (constants) to your hardware's fastest zero-wait-state memory banks.

### Replace Bit-Shifting with Byte-Swapping
* **The Problem:** The eZ80 lacks a barrel shifter. Shifting a 24-bit integer by an arbitrary amount requires looping a 1-bit shift instruction multiple times. Writing `uint24_t x = y >> 8;` forces an explicit 8-iteration shift loop.
* **The Fix:** Align your bitwise shifts to multiples of 8 bits whenever possible. The compiler can optimize a shift or mask of exactly 8 or 16 bits into a zero-cost operation, such as omitting a byte-read or reading the upper register byte (`HLU` or `H`) directly.

### Exploit Block Memory Instructions (`LDIR` / `CPIR`)
* **The Strategy:** Do not write manual `for` loops to copy arrays or clear memory buffers. Always rely on standard C library string and memory utilities: `memcpy()`, `memmove()`, and `memset()`.
* **Why it works:** Modern eZ80 C compilers heavily optimize standard library memory operations directly into the CPU's hardware-accelerated block-transfer (`LDIR`, `LDDR`) or block-search (`CPIR`, `CPDR`) assembly loops.

### Use Power-of-Two Array Sizes to Avoid Division
* **The Problem:** The eZ80 completely lacks a hardware division instruction. Modulo (`%`) and division (`/`) operators trigger slow software math library subroutines.
* **The Fix:** Keep your array dimensions, matrix widths, and circular buffer sizes strictly mapped to powers of two (e.g., 16, 64, 256). This enables the compiler to optimize the operation, swapping out division for an instantaneous bitwise AND operation (`index & 255`).

### Inline Small, Critical Functions
* **The Problem:** Function calls introduce a heavy penalty because the CPU must push the 24-bit program counter onto the stack, jump, and pop it back off upon returning.
* **The Fix:** Use the `inline` or `static inline` keyword for small, frequently called helper functions (such as pixel plotting, bit masking, or mathematical macros) inside inner loops. This eliminates the `CALL` and `RET` overhead entirely by embedding the code directly into the instruction flow.

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
