# What dzap learns that zap can have

dzap assembles the easy case, so some of what makes it fast is the easy case
paying for itself and some is technique that would work anywhere. Those are
worth telling apart as they happen rather than reconstructing later, because
the first kind is free to port and the second kind has already lost once when
it was tried on zap.

**Every dzap change gets a row here, with its measured number and a verdict.**
Three verdicts:

- **Portable** -- nothing about it depends on there being no labels,
  expressions or fixups. Apply to zap and measure on the real benchmark set.
- **Conditional** -- works in zap only after something else changes, named.
- **Not portable** -- the simplification is doing the work.

## The table

| change | measured on dzap | verdict |
|---|---|---|
| Precomputed mnemonic lengths (was a `strlen` per candidate, 8% of all work) | part of −9.9% | **Portable.** Re-deriving a compile-time constant is a defect anywhere. |
| Character class table for space/name/digit | part of −9.9% | **Portable.** zap's lexer classifies bytes with the same compare chains. |
| Bucket mnemonics by first letter *and* length | part of −9.9% | **Conditional** on Option A in `positional-lexing.md`. dzap can assume a statement start is a mnemonic; zap's lexer is context-free and must find registers, directives and flags through the same lookup. |
| Precomputed row modes and `F_CCOK`, one 16-bit compare per row | part of −3.7% | **Portable.** Pure table preparation; zap runs the identical test in its own `match_row`. |
| Operand cleared by copying a zeroed template | part of −3.7% | **Conditional** on shrinking zap's operand first. dzap's is small; zap's is **153 bytes on the eZ80**, 128 of it the expression buffer for deferred fixups. Copying that would be worse than the ten stores it replaces. |
| Literal fast path: read `0x…` and decimal without `num_parse` | part of −3.7% | **Not portable.** Already tried on zap and reverted: **+3.4% on bbcbasic**, faster only on literal-dense synthetic input. In zap an operand can be a symbol or an expression, so the fast path must fall through to the general evaluator and the fall-through is what costs. dzap's version works because that case does not exist. |
| Register masks held as `uint24_t` | dzap **−9.4%** measured alone | **Does not transfer.** Three variants tried on zap, all slower: repacking `isa_row`'s field type +0.5…1.0%; narrow side arrays with the operand left at 32 bits +2.6…3.9%; both together, which is dzap's exact shape, +4.0…6.5%. Try 2 isolates the indirection at ~3%, and try 3 adding narrowing on top makes it worse -- which points at zap's `operand` being **153 bytes**, so narrowing `reg` shifts everything after it including the 128-byte `expr` array, on a struct whose fields are read constantly. dzap's operand is 28 bytes with nothing to misalign. |
| Mnemonic's `.` folded into the class table | part of −12.3% | **Portable.** |
| Immediate held as `int` (24-bit) rather than 32-bit `value` | part of −4.7% | **Portable.** An instruction's immediate is at most three bytes; zap's `operand.imm` is the same `value` type and its emitter has the same ceiling. Invisible on the host, like the register masks. |
| No pre-scan for the line end — one pass over the source instead of two | part of −4.7% | **Conditional**, and closer to a redesign than a transplant. zap is driven token-by-token through `lex_next` rather than line-by-line, so the idea (nothing needs the line bound, because no scan can cross a newline) applies but the shape does not. |
| Literal and displacement accumulators narrowed to `int` | part of −2.2% | **Portable.** Third application of the idea behind the two biggest wins; an operand is at most three bytes and a displacement one signed byte. |
| Output reserved once per instruction, not bounds-checked per byte | part of −2.2% | **Portable, and half-built already.** zap has `pr_reserve`; it is `pr_wbyte` still testing on every byte that would change. |
| **Row rejected on the cheap test before the expensive one** | **−23.2%** | **Portable, and the largest win measured.** zap's `match_row` has the identical structure and the same two expensive terms. See the note below: it contradicts the branchless guidance, which needs qualifying rather than discarding. |
| Short mnemonics packed into a word and compared in one operation | **+1.3% — reverted** | **Not portable, and not wanted.** Bucketing by letter and length already leaves one or two candidates, so the compare loop it replaced was two or three characters and building the packed key cost more. Recorded so it is not re-invented. |
| Constant shifts folded into lookup tables (`<< 3`, `<< 4`) | **−1.6%** | **Portable.** The eZ80 has no barrel shifter, so a shift that is not a whole number of bytes is a loop — `ld b, n; call __bshl`. zap folds operand indices into opcodes with the same `<< 3` and `<< 4`. |
| Immediate bytes read from the field rather than a local | **−1.5%** | **Portable.** zap's emitter writes the same one, two or three bytes and has the same choice about where to read them from. |
| Hot functions kept out of `main`, so every frame fits ix's range | **−7.8%** on pure, **−28.3%** on the row-heavy shape | **Portable, and zap has it worse.** See the section below. |
| Row data in one record per row, walked by a pointer | **−10.1%** on pure, **−35.0%** on the row-heavy shape | **Portable.** zap's `match_row` runs the identical test and reads `regsetA`/`regsetB` as `uint32_t` straight out of the isa table. |
| Register sets as separate byte planes | **+8.5% — reverted** | Recorded so it is not re-invented: it removes the right calls and replaces them with eight `ld hl, base; add hl, bc; ld a, (hl)` sequences per row. The same split *inside one record* is the row above. |
| Register set split into byte planes in the operand, not in match_row | **−3.1%** on `nop`, −2.2% on the mix, −6.5% row-heavy | **Portable.** zap holds the same set as `uint32_t` and masks it in the same places. Every mask on it is a call to `__iand`; split, they are byte ANDs, and `emit_row` went from eleven library calls to none. |
| First digit taken out of both decimal accumulator loops | **−6.9%** on `bit 3, (iy+4)`, −2.0% on the even ISA mix | **Portable.** zap's `num_parse` accumulates the same way. `d * 10` is a call to `__imulu`, and inside the loop a one-digit value pays it to multiply zero by ten -- which is nearly every displacement, and `im 2`, `rst 0`, `bit 3` besides. **Note this one is invisible in generated assembly:** the call site stays for longer values, so the static count is unchanged and only what executes differs. |
| Hex value assembled in three fixed steps rather than a loop | **−1.9%** on `ld hl, 0x123456`, −0.6% on both ISA mixes | **Portable.** A value is at most three bytes, so the loop could only run three times while paying a byte index, a bound test and an indexed store for the generality. |
| Shift folded into a combined hex-nibble table | **neutral — reverted** | Recorded so it is not re-tried. `shl4[hexval[c]]` is two dependent lookups and collapsing them to one changed nothing measurable on either ISA mix, at a cost of 256 bytes. Dependent table lookups are not what is expensive in that parse; 2,163 of its cycles are still unaccounted for. |
| Unbounded scans, with a sentinel newline past the buffer, except the two the compiler mishandles | **−5.1%** on `bit 3, (iy+4)`, −3.3% on `ld a, b`, −0.6% on the mix | **Portable, with a warning attached.** zap's lexer tests a bound on every character of every scan for the same reason, and the same sentinel would remove it. The warning is that two `num_ch` scans had to keep their bound: unbounded they compile to a loop that never tests its first character and stops one short, which no host test can see. Check the generated assembly for every scan converted, and test on hardware. |
| Instructions looked up by pointer rather than by index | **−20.5%** on `nop`, **−13.0%** on the mix | **Portable, and the second largest win measured.** Every use of an index is a subscript, and a subscript is the index times a struct size, which is a call to `__imulu`. zap's `enc_instruction` takes the same index and pays the same multiplies. |
| `transform` call skipped when the type is TR_NONE | **−5.4%** on `nop`, −2.5% on the mix | **Portable.** A load and a compare instead of a call, a dispatch and a return, on the commonest case. |
| Operand parser split so the empty case skips the big prologue | **+2.0% — reverted** | Recorded so it is not re-invented. The premise was wrong: `assemble_line` already assigns `dop_none` directly when there is no comma, so the empty path is only reached by genuinely operandless instructions. The extra call lands on every other line. |
| `reg_of_text` returning a prebuilt descriptor instead of four out-parameters | **+0.4% — reverted** | Recorded because it looks like the change that won 20.5% on the instruction lookup, and is not. There the pointer came out of a data structure and replaced a multiply. Here the pointers are twenty-eight compile-time constants in switch arms, and the compiler hoists two of the addresses into the frame prologue — `ld de, _rd_a; ld (ix-17), de` — paid on every operand, including ones that never reach a register. The frame grew 31 bytes to 33. It does remove a call to `__ishru`, and still loses. |
| Register's first character classified with one table load | **−1.5%** on `ld a, b`, −0.6% on the mix | **Portable.** zap's lexer asks the same question the same way, `name_ch(c) && !digit_ch(c)`, which is two loads of one byte and two masks for one bit of information. |
| Register bytes written inside the switch that recognises the register | **−4.9%** on `ld a, b`, −2.3% on the mix | **Portable, and the lesson matters more than the change.** This removes exactly the `__ishru` the descriptor row above removed, and wins where that lost. A constant shift folds only while the value is still a constant; after a switch joins, `bit >> 16` is a runtime shift and so a call. Sinking the stores into the arms keeps them constant without creating anything new to take the address of. |
| **Rows sorted by mode, with a pointer to the next different one** | **−15.5%** on the row-heavy shape, **−5.0%** on the mix | **Portable.** zap's `match_row` walks the same rows in the same order and has the same 57-row `ld`. The one thing to carry across with it: the jump must hold a *pointer*, not a stride — `ri += skip` was 2.5% slower than no skip at all, because a variable stride times a struct size is a call to `__imulu`. |
| Hex literals assembled a byte at a time, not `acc = (acc << 4) \| d` | **−2.6%** on six-digit immediates, neutral elsewhere | **Portable.** zap's `num_parse` accumulates the same way. Narrow: the compiler will not turn even `<< 8` into a byte move, so every hex digit was a call to `__ishl`, but that is a smaller share of a literal's cost than the shape timings suggested. |
| First letter to bucket base as a table, replacing a multiply | **−2.0%** on pure, **−2.4%** on `nop` | **Conditional**, on the same thing as the length buckets themselves — zap's lexer is context-free and does not know a statement start is a mnemonic. The *technique* is portable and the multiply is the point: `letter * NLEN` is a call to `__imulu`, because MLT is 8-bit and this is an int. |
| Immediate's low byte read as a byte, not masked off the int | **−0.4%** isa_real, −0.7% isa_even | **Portable.** zap folds the same three bits of the same immediate into an opcode. The lesson is narrower than the change: `x & 7` on an int is a call to `__iand` because the value arrives in `hl` and is masked where it sits, and **casting to `uint8_t` first does not help** -- the cast folds away, since masking three bits off the low byte and off the whole value give the same answer. The *load* has to be a byte load. |
| One output cursor per instruction, not `out[pos++]` per byte | **−0.4%** isa_real, −0.4% isa_even | **Portable, and the other half of the reserve row above.** `pr_wbyte` reloads the base and the position, adds them, stores the byte and stores the position back, once per byte. The reservation is what makes a bare cursor safe: room for the longest form is already there, so nothing between taking the cursor and writing it back can move the buffer. |
| **Second operand tested only if the first matched** | **−2.3%** isa_real, −2.1% isa_even | **Portable.** zap's `match_row` builds the same two 0/1 values and ANDs them, computing both before looking at either. Nothing there needs a 0/1 -- the question is whether the operand shares a bit with what the row accepts. Third application of the cheap-test-first row above, and the counts say why it is worth more than it looks: of 4.08 rows examined per instruction, 3.40 reach the register test and A alone rejects 2.00 of them. |
| **A group index per mnemonic, replacing the skip pointer** | **−1.9%** isa_real, **−2.5%** isa_even | **Portable, and it supersedes the sorted-rows row above.** Rejecting a mode cost a whole turn of the row loop -- counter test, row pointer into `iy`, mode, ccok, skip, next -- twenty instructions to step over rows that could not match, and the rows that did match paid the mode test again on every one. The modes belong in a table of their own, five bytes a group; a row reached through its group carries no mode test at all. 114 mnemonics, 170 groups, none with more than seven. The four with condition-code rows (`call`, `jp`, `jr`, `ret`, ten rows between them) stay ungrouped and keep the per-row test, which is exactly what lets every other mnemonic drop it. |
| ccok not tested on grouped rows | **−0.4%** isa_real, −0.4% isa_even | **Portable**, and it falls out of the row above: a mnemonic with a condition-code row anywhere in it has no groups, so every grouped row has the flag clear. |
| One register plane read by index instead of three ANDed | **+6.2% — reverted** | Recorded so it is not re-invented, and it is the most convincing-looking loss so far. An operand's register mask is one-hot, so `(&ri->a0)[plane]` replaces three loads and three ANDs with one of each. It costs 6.2% because the variable index has to be added to the row pointer, which means the row pointer has to be in `hl` to take the `add`, which means it is not in `iy` -- so every other field of the row, and the row pointer itself on the way round the loop, goes through the frame. **Inside a loop over a structure, constant offsets from one index register beat a computed address even when the computed address replaces several constant ones.** Now in the optimization guide. |
| Mode refined by register class (r8 / r16 / index / index halves / I,R,MB) | **simulated, not taken** | Recorded with its numbers so it need not be measured again. Simulated against the real walk with zero mismatches: full register tests fall 3.40 to 1.11 per instruction, but group rejections rise 0.68 to 5.75 and eat the gain. Refining only the B side is the best of the three variants -- 1.79 full, 2.22 rejections -- and is worth about 2% against the group table, which did not pay for a class table, a duplicated row and a per-instruction code computation. One row of 322 spans two classes (A = IX|IY, B = BC|DE|IX|IY), so any such scheme needs a row to be enterable from more than one class. |
| **The whole register test, priced by duplicating every row** | **3.4% of runtime — the ceiling on all of the above** | Measured, not decomposed, and it retires this area. Every row is written into `rowtab` twice, which leaves the output byte-identical (same md5) because a matching row is still found at its first copy, while every *rejected* row is tested twice and every walk to a match is twice as long. That doubles the register-test work and costs **+0.16s of 4.70s on isa_real**, so the existing test is worth 0.16s, or 3.4%. Removing it entirely -- not making it cheaper, removing it -- could not pay for itself twice over. The two rows above are attempts to spend a table restructure on a share of 3.4%, and neither is worth it. **Price a loop before optimising it: 3.74 register tests per instruction sounds like the hot spot and is not one.** |
| **The mode group walk, priced by duplicating every group** | **0.4% of runtime** | Every group written into `grptab` twice. A group is found at its first copy so the rows walked do not change, while every group rejected on the way is rejected twice. Costs +0.02s of 4.70s. The group index is finished: there is nothing left in it to win. |
| **The mnemonic bucket chain, priced by a decoy per entry** | **6.4% of runtime** | A decoy ahead of each mnemonic in its bucket, same first character and same length, differing in the last character so the compare runs to the end before failing. Doubles the chain walk; the real entry is still found. Costs +0.30s of 4.70s. Against `mnemonic_of` as a whole this says the compare loop is about half of it and `bucket_of` and the call are the other half. |
| **The name run handed to the literal path, not scanned again** | **−4.3%** isa_real, −3.3% isa_even, −4.1% degenerate | **Portable.** A name that is not a register is re-read as a literal, because a hex constant with a trailing `h` starts with a letter. C_NUM is C_NAME plus `$`, `#` and `%`, so the second scan reads the same characters and stops in the same place unless the character that ended the name run is one of those three -- one class test against a whole second pass. What reaches that line is mostly labels, which are the longest thing an operand can be. zap's lexer has the same rewind for the same reason. |
| **Local labels in a table of their own, emptied per scope** | **+2.7%** isa_real, +2.2% isa_even, and the feature | **Portable, and the shape is the transferable part.** The reference keys a local as the enclosing global name with the local appended -- `outer:` plus `@aa:` is one entry spelled `outer@aa`, which its "Label already defined" message confirms. Building it that way costs the scope name again in the arena and on every hash and every compare, and a scope name averages seventeen characters here against three for `@aa`. The semantics give a better shape for free: a local can only be satisfied inside its own scope, so when a scope ends every local in it is finished with, and the names, the nodes and the pending references are all reused by the next scope. Local memory is then the high-water mark of one scope rather than the sum of all of them, and an undefined local is reported against the line that used it instead of at the end of the source. |
| **Anonymous labels, without the second pass** | **+0.8%** isa_real, +0.4% isa_even | **Portable.** `@@` reached by position, `@b`/`@p` above and `@f`/`@n` below; 171 definitions and 238 references in the corpus. The reference does it with a temporary file -- pass 1 writes every address into it, pass 2 reads them back keeping a previous and a next -- and one pass needs neither. Backward is not a reference at all, since the address is already known, so it never touches the fixup list. Forward is a symbol with no name and no bucket: every `@f` since the last `@@` points at the same one, and writing the next `@@` defines it. No new machinery, and zap has the same fixup list to hang it on. |
| **One Pearson pass, with the top three bits taken from what is already in hand** | **−4.0%** isa_real, −4.3% isa_even, −4.5% degenerate, −3.1% memory | **Portable, and the largest single win since the row work.** A Pearson hash yields eight bits and the table wants eleven, so the key ran the name twice with a different seed -- doubling the per-character work, which *is* the cost of a Pearson key, priced at 6.1% of runtime by duplicating the call. The three bits come from the first character, the last, and the length instead: all in hand, none costs a pass. Over the corpus's 7,684 global labels expected probes go 2.882 to 2.873 -- marginally better -- and per file both are 1.000. The length alone is not enough and not subtly so: uniform-length names then reach 256 of 2,048 buckets and isa_memory goes 4.478 to 28.427. Dropping the second pass also freed the registers it was spilling: the loop went 29 instructions per character with four frame stores to 17 with none, and a `uint8_t` countdown in place of the `int` index took it to **13**. |
| The last mnemonic character taken out of the compare loop | **neutral — reverted** | Recorded so it is not re-invented. The bucket has already settled the first character and the length, so comparing the last one before the loop removes an iteration from every mnemonic: 2.55 iterations on average become 1.55, and the twelve two-character mnemonics do not enter the loop at all. In the generated code the standalone compare is 7 instructions against 17 for an iteration, so it should save about ten per lookup on 22,457 lookups. It measured 4.74s against 4.76s -- nothing, and the wrong way round. **A saving computed from instruction counts inside a function this size is not a saving**; the compiler reorganises around the change and the register pressure decides. Reverted rather than kept, because a branch and a comment that buy nothing are worse than neither. |
| `mnemonic_of` taken out of line, so its loop gets registers | **+0.4%** isa_real — reverted | The compare loop is 17 instructions per character because `assemble_line` is 3,500 lines long and keeps both pointers in the frame; a function of its own would have `(hl)` and `(de)` free. It loses because the bucket subscript becomes `call __ishl` -- `bucket_head[b]` is `b << 2` and a 24-bit shift is a library call here -- and that is one call on every line of the source against a loop that runs 2.55 times. Casting the index to `uint8_t` does not remove it; NBUCKET is 216 and the value fits in a byte, and the shift is emitted anyway. |
| `same_ci` alone taken out of line | **generated no better — not measured** | The other half of the same idea, keeping the bucket lookup inlined so no shift appears. The compiler still spills: 18 instructions per character with three frame accesses, or 20 with a pointer walk, against 17 inlined. Case-folding the source character needs a register the loop does not have, so the tight `ld a,(de) / or a,32 / ld c,a / ld a,(hl) / cp a,c` form never appears however the function is arranged. Rejected on the generated code without spending emulator time. |
| **The mnemonic chain, measured rather than assumed** | **1.41 probes, not the cost** | Weighted by how often each mnemonic actually occurs -- the corpus figures the `real` benchmark is built from -- a lookup examines **1.41** candidates, not the twelve the worst bucket holds. `ld`, `call`, `push`, `cp` and `exx` are all first in their chains already. Reordering chains by frequency would take it to about 1.05, which is 12 instructions a lookup, which is the size of the change that measured as nothing above. **The cost is the successful compare, and that has to read every character of the name.** Three attempts at the 6.1% have measured neutral or worse; the floor looks like 4% and this area is done for now. |
| **The symbol chain compare, four shapes measured** | **the shipped one wins** | `sym_at` still walked its chain with subscripts -- `text[i] == name[i]`, which reloads the length, the arena base and the name from the frame on every character, sixteen instructions for a byte compare. The obvious fix is the pointer walk `loc_intern` already uses, and it generates **24** instructions with six frame accesses instead of sixteen with three: three live pointers do not fit where two and an index do. Backwards from the end measured 4.76s and a pointer-plus-counter 4.76s, against **4.72s** for what is there. Four shapes, and the one nobody would write on purpose is the fastest. |
| **Half of what the symbol table appears to cost is the benchmark** | 4.5% measured, ~2.2% on real code | isa_real's global labels average **17.1 characters**; the 21,829 in the Agon corpus average **8.3**. Every one of them is hashed once and compared once, so the benchmark does 2.1x the per-label character work real code does, and `sym_at` and the key both read about twice what they would. The mnemonic weights in `real` mode come from the corpus and the label *shape* does not, which is worth fixing when the baselines can afford to move again. Second time this has bent a conclusion: `sym_intern` read as 8.1% for the same reason. |
| **Expressions, with precedence by default and `-ez80` for the reference** | **+2.5%** for the evaluator, and see below | **Portable, and the flag is the transferable part.** ez80asm has no precedence at all -- `1+2*3` is 9 -- and ZDS has two levels where `+ - << >> & | ^` are one, so `1\|6&4` is 4 there and 5 anywhere else. Three assemblers, three answers, all measured rather than assumed. dzap does what every language does and offers `-ez80` for byte-compatibility, which is the first place it knowingly computes a different number from the reference on input the reference accepts. One algorithm serves both: a precedence climb where every operator binds equally *is* a left-to-right fold, so the compatible answer is a table and not a second code path. |
| Argument parsing left inline in `main` | **not the cause — hypothesis refuted** | `run` is inlined into `main`, so `main` holds the loop over the source lines, and the option handling added 221 instructions in front of it. That looked like the explanation for the precedence climb costing 5.3% on a build where the evaluator is entered **zero** times -- confirmed by counting: `expr_climb` runs 0 times on isa_real, isa_even and pure. Moving the parser out of line with `noinline` brings `_main` back from 1002 instructions to 838 and measures **5.18s against 5.12s**: slightly worse, not better. Recorded because it is a plausible-looking explanation that is wrong, and the next person will think of it too. |
| The scope switch asked on every line | **3.5% — made lazy** | A global label opens a scope that starts on the *next* line, so the switch cannot happen where the label is. Doing it at the top of the line loop instead costs 3.5% -- more than the whole feature -- to ask a question on 19,399 lines that only a line with an `@` on it can answer. Moved into the local lookup, which is the first place it can matter, comparing the line the label was on rather than a flag: `two: jp @l` reads its operand in the scope `two` is closing, so "is one pending" is the wrong question and "was it this line" is the right one. **A deferred action belongs at the next thing that can observe it, not at the next tick.** |
| Local buckets placed in the middle of `dz` | **moved to the end, and asserted there** | The frame-pointer cliff wearing different clothes. `dz` is reached through a pointer and `iy` displacement is a signed byte, so a field past 127 has its address computed rather than being read in one instruction. The 64 bucket slots are 256 bytes; sitting before `line` and `err` they pushed both out of range, and `line` is written on every line of the source. **A big cold field added to a hot struct belongs at the end of it.** Honest about the number: a `_Static_assert` on the target build proves `dz.line` is out of range in the middle placement and in range at the end, but the two builds measured 4.58s and 4.62s on isa_real -- the wrong way round and inside run-to-run variation, so the addressing is established and a saving is not. The assertions stay because the addressing is the part that can silently come back. |
| Symbol key walked by pointer, and the shift composed from a table | **+1.7% — reverted** | Recorded so it is not re-invented. `sym_bucket` costs twenty-eight instructions per character and spills h1, h2 and the character to the frame on every turn, so the index arithmetic looked like the problem. It is not: rewriting `name[i]` as a pointer walk makes the loop 31 instructions, and a `uint8_t` countdown makes it 27, against 29. Two separate loops instead of one interleaved pass gives 14 + 14, which is the same work in a better shape and still no win. The `hi3` table that replaces `h1 \| (h2 << 8)` removes a `call __ishl` and adds a `call __bshl`. The loop is at its floor: two table lookups and an XOR is what an eleven-bit Pearson key costs. |
| **Parentheses as grouping, told from indirection by position** | **free** -- 4.72/4.84/4.66/5.48s, unchanged on all four | **Portable, and worth more than precedence was**: of the 11 assembly-only ZDS projects in the corpus, 3 assemble today and **8 are blocked on parentheses and nothing else**. The reference has none -- `EQU 1+(2)` is "Unknown identifier" and `EQU 100/(5)` is a SIGFPE -- so ZDS is the oracle and accepting them only widens what assembles. The rule is positional: a `(` that opens the operand and whose match closes it is indirection, anything else groups. The transferable part is *how* the third case is decided -- `add a, (RTABLE-DTABLE)/2` is indistinguishable from `ld a, (var)` until the `/`, and scanning ahead to the matching parenthesis would land on every `(hl)` and `(ix+d)` in the file. Parse it as indirection, look at the character after the `)`, and rewind if it is an operator; the operand start is still in `*pp`, and reinterpreting can only turn an error into a value. zap decides indirection in the same place and can use the same rewind. |
| **Two forward references in one expression** | **−1.7%** isa_real, −1.6% isa_even, −2.1% degenerate, −1.1% memory | **Portable.** `end - start` with neither label written yet, which is how a program measures a table it is still emitting: 678 of the corpus's 1,114 multi-label expressions join them with nothing but `+` and `-`. It is *faster* than what it extends, and no wider: `next_addr` was three bytes recording something `off` already knew -- a relative is always measured from `DZ_ORG + off + 1` -- so dropping it paid for the second symbol pointer and left the fixup at sixteen bytes, still a power of two, still no `__imulu`, still the same memory. The tracking got smaller too: a sign per symbol and a two-bit mask replaces one symbol and a linearity flag, and it picked up `k - later` and `-start + end`, both previously refused. **A record that is already the right size can still be carrying a field it does not need** -- second time here, after the padding that paid for the addend. |
| Second operand passed as a pointer to the shared empty one | **+2.7% — reverted** | 56% of lines in isa_real have one operand or none, and each built an empty 19-byte operand with an `ldir` to hand to row selection. Nothing writes through it, so it can point at the template instead -- and that costs 2.7%, because `&b` was a constant frame address and `b->r0` was `ld a, (ix-nn)`. As a pointer it becomes `ld iy, (ix-mm); ld a, (iy+k)` at every field, in `match_row` and `emit_row` both. **Second time today that replacing a copy with an indirection lost on this chip**; the first is the register plane row above. A fixed frame slot is cheaper than a pointer to anything. |

## The frame-pointer cliff, and what zap has

`ix` displacement is a signed byte. A function whose frame is larger than 128
bytes cannot reach most of its own locals with `ld a, (ix-9)`, so the compiler
emits `ld bc, -139; lea hl, ix + 0; add hl, bc; ld hl, (hl)` instead — five
instructions where there was one, on every access.

dzap fell off it by accident. `run`, `assemble_line`, `match_row` and
`emit_row` were all inlined into `main`, whose frame reached 149 bytes and
whose hot loop paid the detour 23 times. Marking those four `noinline` split
one 149-byte frame into four of 60, 62, 19 and 20, removed every escape, and
made the whole program *smaller* — 2557 instructions against 2634, despite the
calls and returns it added. It was worth 28.3% on the shape that scans the most
rows.

**zap is in the same state and further into it.** Compiled the same way:

    src/encode.c    106 escape sequences,   1 function over 128 bytes
    src/parser.c    191 escape sequences,   6 functions over 128 bytes
    src/operand.c     0
    src/lexer.c       0

That is not an inlining accident — those functions are simply large — so the
fix is not the same `noinline`, but the cost is identical and it lands on the
two hottest files in the program. Splitting the big frames, or moving the
locals a hot loop touches into a small helper, is the shape of it.

The same compile shows the other half. encode.c calls `__land` ten times and
`__lcmpzero` twelve; parser.c calls `__imulu` eleven. The `l` prefix is the
32-bit helper: that is `reg_match` testing `uint32_t` register sets, the exact
code the row-record change above rewrote in bytes.


## Standing note

The host cannot see the `uint24_t` change at all -- `uint24_t` is a typedef for
`uint32_t` under the test stubs, so callgrind reports it as exactly zero
instructions. It was the largest single win measured. Any portability judgement
made from a host profile will miss that whole class of change; the verdicts
above are about *semantics*, and the sizes are always from the Agon.

## Running total

    256 KiB of pure instructions, dzap
      baseline                                    19.72s   1,387 cycles/byte
      + lengths, class table, length buckets      17.76s   1,250
      + row precompute, operand copy, literals    17.10s   1,203
      + 24-bit masks, dot in table                15.00s   1,055
      + 24-bit immediate, single-pass lines       14.30s   1,006
      + narrowed accumulators, reserve per insn   13.98s     983
      + cheap-test early-out in row selection     10.74s     756
      (round 7 tried and reverted -- see below)
      + shift tables, emit_imm, frames, row rec.  8.92s     627
      + hex literals, bucket base table            8.76s     616
      + rows indexed by operand mode                8.32s     585
      + byte planes, pointer lookup, TR_NONE       6.90s     485
      + one class load, register bytes in arms     6.70s     471
      + unbounded scans past a sentinel             6.54s     460

The last two lines are one session's six changes, each measured on its own
against the same build of the same file. That round's baseline re-measured as
11.10s rather than the 10.74s recorded above — about 3% of run-to-run drift
between days — so its steps are quoted against 11.10s and against each other,
never against a number from another day:

    baseline                                     11.10s
      + constant shifts as lookup tables         10.92s   −1.6%
      + emit_imm reading the field               10.76s   −1.5%
      + hot functions out of main                 9.92s   −7.8%
      + row data in one record                    8.92s  −10.1%
      + hex literals a byte at a time             8.94s   +0.2%
      + first letter to bucket base table         8.76s   −2.0%

The hex change is neutral on this file and worth −2.6% on six-digit
immediates alone; it is in because it costs nothing elsewhere, not because it
showed up here.

**The p256 series stops there.** The default benchmark changed to the two
256 KiB sources that hold the whole instruction set -- `isa_even`, every form
evenly, and `isa_real`, every form but weighted by 10,440 instructions of BBC
BASIC and Rokky -- because a forty-form file cannot show what row selection
costs on a table with a 57-row `ld` in it. The rounds between were measured on
those and not on p256, so there is no honest way to fill the gap in the series
above; p256 was re-measured only at the ends of the row-selection round, 5.38s
to 5.18s, 379 to 364 cycles per byte.

    256 KiB of the whole instruction set          isa_real   isa_even
      before the row-selection round               5.34s      5.70s
      + immediate's low byte as a byte             5.32s      5.66s
      + one output cursor per instruction          5.30s      5.64s
      + B tested only if A matched                 5.18s      5.52s
      + a group index per mnemonic                 5.08s      5.38s
      + ccok dropped from grouped rows             5.06s      5.36s

    isa_real  376 -> 356 cycles/byte, 47.9 -> 50.6 KiB/s
    isa_even  401 -> 377 cycles/byte, 44.9 -> 47.8 KiB/s

Output byte-identical to ez80asm at every step, on both sources and on every
case file.

On `ld (ix+8), a` alone, which scans 43 of ld's 57 rows and so shows row
selection undiluted: 42.54s to 19.76s, **−53.6%**.

**66.8% in total.** Roughly two thirds of it is portable or conditional; the
rest is the simplification.

## What an instruction costs

One shape per file, 30,000 lines each, on the 8.76s build. This is what the
feature work has to be priced against: a feature that adds 500 cycles to every
instruction costs about a tenth of the floor.

| instruction | operands | cyc/insn | cyc/byte | out | rows scanned |
|---|---|---|---|---|---|
| `nop` | none | 4,927 | 821 | 1 | 1 |
| `ld a, b` | reg, reg | 6,193 | 619 | 1 | 5 |
| `ld a, 0x42` | reg, imm8 | 6,894 | 530 | 2 | 2 |
| `bit 3, (iy+4)` | imm, (iy+d) | 8,442 | 528 | 4 | 2 |
| `ld hl, 0x123456` | reg, imm24 | 8,614 | 479 | 4 | 1 |
| `ld (ix+8), a` | (ix+d), reg | 12,141 | 809 | 3 | **43** |
| the 40-shape mix | varied | 6,948 | 616 | | |

After indexing the rows by mode:

| instruction | cyc/insn | was | rows walked |
|---|---|---|---|
| `nop` | 4,706 | 4,927 | 1 |
| `ld a, b` | 5,640 | 6,193 | 5 |
| `ld a, 0x42` | 6,930 | 6,894 | 2 |
| `bit 3, (iy+4)` | 8,356 | 8,442 | 2 |
| `ld hl, 0x123456` | 8,602 | 8,614 | 1 |
| `ld (ix+8), a` | **10,260** | 12,141 | 43 → 7 mode groups |
| the mix | **6,599** | 6,948 | |

**585 cycles per byte.** The spread between the cheapest and dearest
instruction is down from 2.5× to 2.2×, and what is left of it is immediate
width and output length rather than row selection.

## How many rows each shape actually walks

The row counts in the older tables above are from **before** the rows were
sorted by operand mode, and stopped describing the code the moment that landed.
Simulating the walk as it is now:

| form | mode wanted | group skips | rows tested | match_row |
|---|---|---|---|---|
| `ld a, b` | 00 | 0 | 1 | 639 |
| `ld a, 0x42` | 02 | 2 | 2 | 1,155 |
| `ld hl, 0x123456` | 02 | 2 | 1 | |
| `ld (ix+8), a` | 10 | 4 | 11 | |

`ld` sorts into seven groups of 14, 14, 5, 5, 12, 2 and 5 rows.

Two things follow. **`ld a, b` is the best case there is** -- its group sorts
first and it matches on the first row, which is why `match_row` costs it least
of any shape. And a skip costs about what a row test costs, roughly 170 cycles
either way, so the index pays only when the groups it steps over are large:
`ld (ix+8), a` clears 38 rows in four hops, which is the whole point, while a
shape whose group sorts early was never paying much to begin with.

This is worth stating because the difference between `ld a, b` at 4,043 cycles
and `ld a, 0x42` at 5,591 looked at first like row selection misbehaving --
fewer rows for more time. It is not: the first figure is simply the floor of
what row selection can cost.

## What each shape costs now

Measured together on one build, so the columns are comparable. "First" is the
earliest measurement of that shape in this work.

| instruction | cyc/insn | first | change | cyc/byte | over the floor |
|---|---|---|---|---|---|
| `nop` | 3,293 | 5,075 | −35.1% | 549 | — |
| `ld a, b` | 4,043 | 6,992 | −42.2% | 404 | +750 |
| `ld a, 0x42` | 5,591 | 7,987 | −30.0% | 430 | +2,298 |
| `bit 3, (iy+4)` | 6,058 | 9,265 | −34.6% | 379 | +2,765 |
| `ld hl, 0x123456` | 6,414 | 9,769 | −34.3% | 356 | +3,121 |
| `ld (ix+8), a` | 8,307 | 25,596 | **−67.5%** | 554 | +5,014 |

| benchmark | cyc/insn | cyc/byte | first cyc/byte |
|---|---|---|---|
| p256, forty forms | 4,886 | 433 | 1,387 |
| `isa_even`, all 1,083 forms | 5,266 | 463 | 493 |
| `isa_real`, real weighting | 5,208 | **435** | 464 |

Three things worth reading off it.

**The spread has collapsed.** The dearest shape was 5.0 times the floor and is
now 2.5. Almost all of that is `ld (ix+8), a`, from indexing the rows by
operand mode and then from the displacement's multiply by zero.

**Cycles per byte runs opposite to cycles per instruction.**
`ld hl, 0x123456` is the second dearest instruction and the cheapest per byte,
because a long instruction spreads the fixed cost over more source. Only a
mixed file gives an honest per-byte figure.

**Quote `isa_real`, not p256.** p256's 433 looks better and is not comparable
to anything: it holds forty hand-picked forms, several of them the ones
optimised hardest, and its apparent 68.8% improvement over the session is
partly that. `isa_real` holds all 1,083 forms weighted like real source and is
the file that caught the operand merge looking good when it was not.

`ld a, b` divides up like this, by the same stop-after-each-stage method:

    read, line loop, scan to the newline      676
    classify the mnemonic run                 283
    mnemonic_of                               393
    two parse_operand calls                 2,089
    match_row                                 602
    emit_row                                1,020

and `parse_operand` itself, by the same method again -- two operands, so halve
these for one:

    clear the operand, skip space, empty test   (base)
    `(` test, name detection, name scan            688
    reg_of_text and the field stores               700
    displacement, mode, closing paren              160

The two middle rows are what the changes above address: one of them was
loading the same class byte twice, the other was shifting a constant that had
stopped being one.

**parse_operand is 41% of it**, and against `nop`'s 676 for the same stage that
is about 700 cycles to recognise a one-character register name. It is not one
hotspot -- the function is 1,013 instructions with a 31-byte frame, and the
register path walks a long way through it.

## Measurement noise

Three interleaved repeats of the same two binaries on `ld a, b`: 8.24, 8.26,
8.24 against 8.28, 8.28, 8.28. The emulator is deterministic to about
**±0.02s, or 0.25%**, so a change under half a percent is not worth claiming
from one run, and a consistent 0.4% is real. This is why the descriptor
experiment above counts as a regression rather than a wash.

## Taking the floor apart

`nop` is the cheapest instruction there is, so what it costs is what every
other instruction is built on top of. Measured by building variants that stop
after each stage, on 30,000 lines:

| stage | at 4,706 cycles | at 3,428 |
|---|---|---|
| read, line loop, scan to the newline | 565 | 578 |
| classify the mnemonic run | 332 | 332 |
| `mnemonic_of` | 1,094 | 627 |
| two `parse_operand` calls | 590 | 676 |
| `match_row` | — | 602 |
| `emit_row` | 2,126 (with match_row) | 811 |

Nothing dominates any more. Three changes got it from 4,706 to 3,428 and the
mix from 585 cycles per byte to 485; all three were the same defect in
different places — an ordinary-looking C operation that is a library call on
this chip.

Two things were measured and left alone. A `dop` copy costs about 25 cycles,
because the compiler does it with `LDIR`, so shrinking the struct would buy
almost nothing. And splitting the operand parser to keep its 31-byte frame off
the empty path cost 2% on the mix, for the reason in the table above.

**About 4,900 cycles is the floor** — read the line, scan the mnemonic, look it
up, match one row, write one byte — and it is paid by every instruction whatever
its shape. Everything else is marginal:

    + a register operand              ~630 each
    + a two-digit hex immediate     ~1,970
    + a four-byte CB-prefixed form  ~3,510
    + a six-digit hex immediate     ~3,690
    + reaching ld's forty-third row ~7,210

Two things follow. Cycles per *byte* runs backwards to cycles per instruction —
`ld hl, 0x123456` is the second most expensive instruction here and the
cheapest per byte, because a long instruction amortises the fixed cost over
more source. Only the mix is a fair per-byte figure. And row scanning was the
largest single variable cost — which is what the mode index below addressed.

## Applying these to zap

| change | dzap | zap |
|---|---|---|
| Row early-out | −23.2% | **−1.2% bbcbasic, −6.4% synth — kept** |
| Character class table | measured alone here | **−1.6% bbcbasic, −3.1% synth — kept** |
| 24-bit immediate in the operand | part of −4.7% | **−0.1% bbcbasic, −0.8% synth — kept** |
| 24-bit register masks | −9.4% | **+0.1% to +6.5% — reverted, five variants.** Fully decomposed below. |
| Instruction head written as one block | part of −2.2% | +0.8% to +0.9% — reverted. Most instructions have one head byte, so building an array and calling `pr_wblock`, which does a memcpy, costs more than the single `pr_wbyte` it replaced. |
| Precomputed row modes | part of −3.7% | **Not measured, and not worth it.** It needs the same `row_base` indirection that try 2 of the masks isolated at +2.6…3.9%, to save two masked loads per row. |

Cumulative on zap: bbcbasic 18.42s → 17.88s (−2.9%), rokky 2.28s → 2.20s
(−3.5%), synth 38.06s → 34.28s (−9.9%). synth's ratio against ez80asm goes
0.89x → 0.75x.

### The register masks, taken apart

Five variants, each measured on its own, against a baseline of 17.88s bbcbasic
and 34.28s synth:

| variant | narrowing | repacks | indirection | bbcbasic | synth |
|---|---|---|---|---|---|
| table side only | half | `isa_row` | no | +0.1% | +0.3% |
| operand side only | half | `operand` | no | +1.3% | +2.4% |
| both sides | full | both | no | +0.5% | +1.0% |
| side arrays, operand wide | half | no | yes | +2.6% | +3.9% |
| side arrays, operand narrow | full | no | yes | +4.0% | +6.5% |

Nothing beats leaving it alone. The two costs are visible separately: the
indirection is worth about 3% wherever it appears, and half-narrowing costs
more than not narrowing at all because the compare promotes the narrow side
back. Narrowing the table alone is free only because the smaller struct pays
for the widening it causes.

The same narrowing is worth 9.4% in dzap. The difference is not the code — it
is that dzap's hot loop is nearly its whole program and its operand is 28 bytes,
where zap reaches this code far less often per source byte and its operand is
153.

**Three of six transferred.** The two that paid most touch neither the data
layout nor add indirection: an early-out that skips work, and a table that
replaces comparisons. The three that failed all either added a level of
indirection or repacked a shared struct. That is the rule this exercise
actually produced -- not "narrow your types", but *change control flow and
lookup tables freely; be very careful with layout and indirection.*

The spread on the early-out is the shape to expect from all of these: synth is
11.3 source bytes per instruction and takes the full benefit, BBC BASIC is 41.5
and most of its bytes never reach the code being changed. A change worth *n*%
in dzap is worth a fraction of that in zap, scaled by how much of the real
source is instructions.

**Bundle nothing.** The masks entry above was wrong because two changes shared
one measurement.Each change gets its own run, in both programs.

## Round 7: both halves lost

| | 256 KiB | vs baseline |
|---|---|---|
| baseline | **10.74s** | |
| skip the first character in the mnemonic compare | 11.20s | +4.3% |
| ...and merge `row_modes` with `row_ccok` | 12.76s | +18.8% |

**Skipping a known-equal character costs more than it saves.** The bucket
already agrees on the first letter, so `same_ci(name + 1, s + 1, n - 1)` looked
free — but it adds two pointer additions and a subtraction per candidate to
remove one iteration from a loop that runs one to three times.

**Packing a flag into the spare bit of a `uint16_t` cost about 14%.** It
replaced a byte load and a 16-bit compare with a 16-bit load and two 16-bit
`AND`s. The guide's §1 says 16-bit is the worst width in ADL mode because the
architecture has no dedicated 16-bit truncation and the compiler must mask the
upper 8 bits of a 24-bit register. That is the obvious explanation and the
direction is right, but this measurement does not isolate it: the change also
removed a byte load and altered the comparison, so it is evidence for the
guide's entry rather than a clean verification of it. The entry stays marked
unverified.

Both reverted. Neither is portable, since neither is an improvement anywhere.

## Branchless is not unconditional

`ez80_advanced_optimization_guide.md` says data-driven beats chains of if/else
on a chip with no branch predictor, and that was measured and is true -- for a
branch that only **selects a value**. The row early-out **skips work**: one
compare of a precomputed value avoids two `reg_match` calls, on three rows out
of four. That is worth far more than the pipeline costs, and it was the single
largest win in this whole exercise at 23.2%.

zap's `match_row` was deliberately made branchless earlier in the project for
the reason that does not apply here. Carrying this over is likely the most
valuable single thing dzap has produced.
