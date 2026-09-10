/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ZAP_H
#define ZAP_H

/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * zap -- an assembler for the Agon Light, in one pass and one file.
 *
 * Two goals, both load-bearing: agree with `ez80asm` byte for byte, and be
 * fast enough that assembling on the Agon itself is practical. Where the
 * reference does something surprising -- no operator precedence, `IF a == b`
 * evaluating `a` and discarding the rest, `0bh` reading as hex before binary
 * -- `-ez80` reproduces it rather than being right and incompatible.
 *
 * ONE PASS. A reference to a label further down cannot be resolved where it is
 * read, so the output is built in memory and the references to it are patched
 * at the end. Everything else follows from never looking at a line twice.
 *
 * ONE FILE, and not for the usual reasons. The eZ80 has no cache and a
 * function call is expensive, so what matters is how much of the hot path the
 * compiler can see at once: `assemble_line` has the operand parser, the row
 * match and the emitter inlined into it.
 *
 * EVERY CHARACTER SCAN IS BOUNDED. A loop that walks a character pointer must
 * compare that pointer against the end of the buffer as well as testing what
 * it points at:
 *
 *     while (p < e && is_space_ch(*p)) { p++; }
 *
 * The same loop with the `p < e` half left out is the mistake.
 *
 * This is not defensive programming. Without the bound the compiler is free to
 * rotate the loop -- pre-decrementing the pointer and testing one character
 * past it -- so the first character is never examined and the scan stops one
 * short. The host build is correct either way, so only the source or the
 * generated assembly shows it. `test/run.sh` checks that every scan here keeps
 * its bound.
 *
 * THE ORDER OF THIS FILE. Each section is introduced by a banner comment:
 *
 *     types and state          what an operand, a symbol and a fixup are,
 *                              and the one state object everything reads
 *     symbols                  the global table, its storage, and fixups
 *     local labels             @name, @@, @f and @b, and their scopes
 *     output                   the buffer the bytes are built in
 *     mnemonics                the instruction table and its lookup
 *     registers and flags      recognising operands that are registers
 *     scanning                 character classes and literal readers
 *     expressions              the evaluator and its forward references
 *     equ
 *     macros                   definition and expansion
 *     directives               everything that is not an instruction
 *     selecting and emitting   row matching and byte output
 *     the line loop            assemble_line, reporting, and main
 *
 * docs/DESIGN.md describes how those parts fit together.
 */

#include <stdbool.h>

#include <stdint.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#ifdef ZMALLOC
#include "zmalloc.h"
#define Z_SITE(x) (z_site = (x))
#else
#define Z_SITE(x) ((void) 0)
#endif

#include "buf_reader.h"

#include "isa.h"

#include "registers.h"

#include "timing.h"

#include "value.h"

#ifdef AGONDEV
#include <agon/mos.h>
#else
#include "agon/mos.h"
#endif

/* The word an expression is evaluated in.
 *
 * Four bytes, where the eZ80's word is three. This is deliberately wider than
 * the machine, and it is the only quantity here that is: an address, a count,
 * an immediate and a displacement all fit in 24 bits, and widening those would
 * cost something on every line.
 *
 * An expression has to be wider because `DW32` and `BLKL` are four bytes wide.
 * The reference fills them with values like `0x55555555` and `-2147483648`,
 * and no care at the emitter recovers a bit the evaluator has already dropped.
 * The reference evaluates in 32 bits, so this is the width that agrees with it.
 *
 * Most operands never reach the evaluator: a register, a plain literal and a
 * bare name each have a reader of their own. Those readers work in the
 * machine's word and hand anything wider to `num_parse`, which is what keeps
 * the extra width off the common path.
 *
 * The assertion is here rather than in a comment because narrowing this is a
 * one-line edit that would look like free speed. It fires on the target and
 * not on the host, where `int` is four bytes already. */
typedef value evalue;

_Static_assert(sizeof(evalue) >= 4,
               "DW32 and BLKL need an evaluator wider than the eZ80 word");

/* The mode the machine starts in. `ASSUME ADL=n` moves it, and may move it
 * again: the reference lets a file switch back and forth, and the width of an
 * immediate follows wherever it is at that line. */
#define ZAP_ADL  true

#define ZAP_ORG  0x040000

/* The output buffer is sized once, from the source, and never doubled.
 *
 * Doubling needs the old block and the new one at the same time: growing to
 * 512 KB asks a 512 KB machine for 768 KB, and 2 MiB of these instructions
 * wants exactly that. It failed at line 64,727 with nothing to say but "out of
 * memory", which is the memory wall the notes describe, reached by a program
 * that does no bookkeeping at all.
 *
 * These instructions average a shade under a fifth of a byte of output per
 * source byte. A quarter is a comfortable margin over that and still fits, and
 * the growth path below exists only so a denser source is refused rather than
 * silently truncated. */
#define OUT_SHIFT 2

#define OUT_MIN   (16 * 1024)

#define OUT_STEP  (32 * 1024)

#define BUF_KB    16

/* An operand, without what a general assembler needs it to carry.
 *
 * zap's operand is 168 bytes, 128 of them the text of an expression kept in
 * case it has to be evaluated again later. Nothing here is ever evaluated
 * twice, so the whole field goes, and with it the cost of having an operand at
 * all: two of these are built for every instruction in the source. */
typedef struct sym sym;

typedef struct _dop {
    /* The register set, as three byte-wide planes of one bitmask.
     *
     * The mask's highest bit is R_I at 2^20, and every use of it is a mask or
     * a test against zero, never arithmetic. Held as a single 24-bit word each
     * of those would be a helper call, because AND on this chip is an 8-bit
     * instruction; split into bytes they are instructions the chip has.
     *
     * The split happens where the register is recognised, not where it is
     * used, so an instruction with no register operands never pays for it. */
    uint8_t r0, r1, r2;

    /* Whether the three planes above are all zero, recorded where they are set
     * rather than re-derived. The row matcher asks this for both operands of
     * every instruction; deriving it there would be six loads and four ORs to
     * learn something the operand has known since it was built. */
    uint8_t noreg;
    uint8_t reg_index;
    bool cc;
    uint8_t cc_index;
    uint8_t mode;

    int disp;
    /* The label this operand named, when it is not defined yet. imm holds
     * nothing useful in that case; the emitter records a fixup once it knows
     * where the bytes land, and run patches them all at the end. NULL on every
     * operand that is not a forward reference, which is most of them. */
    const sym* fwd;

    /* A second one, subtracted or added. `end - start` with neither written
     * yet is how a program measures a table it has not finished emitting, and
     * it is the common shape among operands that name two labels ahead.
     *
     * Two is the limit, and not an arbitrary one: the fixup that carries them
     * is sixteen bytes so that indexing it is a shift instead of a multiply,
     * and two symbols is what fits. */
    const sym* fwd2;
    bool fwd2_neg;

    /* An instruction's immediate is at most three bytes -- a 24-bit address in
     * ADL mode -- so it is held in the machine's own word rather than in the
     * evaluator's wider one.
     *
     * Truncating from `evalue` on the way in is what the emitter would do
     * anyway: it writes the low one, two or three bytes and never looks at the
     * rest. */
    int imm;
} dop;

#ifdef AGONDEV
/* Pinned, because every byte of this struct is copied twice a line from the
 * empty template, and the way a struct like this grows is one innocent `bool`
 * at a time. A new flag belongs in `mode` if a bit will do.
 *
 * Twenty-one is not a power of two and does not need to be: nothing indexes an
 * array of these. */
_Static_assert(sizeof(dop) == 21, "an operand is twenty-one bytes");
#endif

/* Room for the longest instruction, asked for once.
 *
 * Testing on every byte written meant a bounds check per output byte when an
 * instruction knows in advance that it cannot need more than a handful. The
 * longest form here is two prefixes, an opcode, two displacements and two
 * three-byte immediates. */
/* The longest instruction is twelve bytes, and a mode suffix puts one more
 * byte in front of it. */
/* The longest label the reference takes, counting the `@` of a local. */
#define LABEL_MAX 64

#define OUT_MAX_INSN 13

/* How much of a failing line is echoed back. Longer than any line anybody
 * writes, and a line longer than this is truncated rather than refused --
 * the report is a courtesy and must never itself be a failure. */
#define ERRLINE_MAX 128

/* What the reference takes on one line, and what it counts: everything up to
 * the newline, a carriage return included. */
#define LINE_MAX_CHARS 256

/* A listing row under the first one: six characters of address left blank, a
 * space, twelve of output field, and the newline. The first row is longer by
 * whatever its source line was, so its length is recorded rather than
 * computed. */
#define LIST_ROW_LEN 20

/* The longest file name INCLUDE and INCBIN will take. Fixed, because the name
 * is copied into a frame that has to outlive the line it came from, and into
 * `zap_state.errpath` when an include fails. */

/* ======================================================================
 * SYMBOLS
 *
 * Global labels and EQU values: the table they live in, the key that finds
 * a bucket, and the fixups that record a reference to a label the source
 * has not defined yet.
 * ====================================================================== */

/* A label and its address.
 *
 * The name is copied rather than pointed at: source lines live in the reader's
 * buffer and are gone as soon as it refills, so a pointer into one would point
 * at a different line by the time a forward reference is resolved.
 */
#define INCLUDE_NAME_MAX 80

/* How many parameters a macro may take. The reference's own corpus does not go
 * past four; sixteen is more than any of it and keeps the argument spans on the
 * stack rather than in another allocation. */
#define MACRO_MAXPARAM 8

typedef struct _macro macro;

/* Where a parameter appears in a macro body, found once when the body is read.
 *
 * A body does not change after ENDMACRO and neither do the places its
 * parameters occur, so they are located once at definition time. Doing it at
 * expansion time would mean classifying every token of every body line and
 * asking every parameter about every identifier, on every invocation.
 *
 * These are walked with a pointer and never subscripted, which is why the
 * record is its natural size rather than padded. `marks[i]` would be a call to
 * __imulu -- the eZ80's multiply is 8-bit -- and no amount of padding makes
 * the record a power of two on both the Agon and the host, because `int` is
 * three bytes on one and four on the other. The marks are in body order, so a
 * single cursor walking forward covers every line. */
typedef struct {
    int off;        /* where in the body the name starts */
    uint8_t k;      /* which parameter */
    uint8_t len;    /* how many bytes of body the argument replaces */
} macmark;

/* One listed line that has a fixup in it, and everything needed to write its
 * byte columns again once the fixup is settled: where the line starts in the
 * listing file, how long its first row is -- the rows under it are a fixed
 * width -- and which bytes of the output it printed. */
typedef struct {
    int lstat;      /* offset in the listing file of the line's first row */
    int row0;       /* length of that row, through its newline */
    int outoff;     /* the line's first byte, as an index into state.out */
    int nbytes;     /* how many bytes it printed */
} lstfix;

struct _macro {
    macro* next;
    const char* name;
    uint8_t namelen;
    uint8_t nparam;
    const char* params;   /* the parameter names, each preceded by its length */
    char* body;           /* the lines between MACRO and ENDMACRO */
    int bodylen;
    int bodycap;

    macmark* marks;       /* where its parameters are, in body order */
    int nmarks;
    int markcap;

    /* Where the MACRO directive is, so a failure in the body is reported
     * against the line of the *file* it was written on rather than against its
     * index in the body. The reference does the same: a body line is "line 2"
     * of the source, not "line 1" of the macro.
     *
     * The path is copied into the arena rather than pointed at, because a
     * reader's file name lives exactly as long as the reader and a header of
     * macros is normally defined in one file and used from another. */
    const char* defpath;
    int defline;

    /* Whether the body mentions a local label at all -- defines one, or names
     * one. Decided as the body is read, for the same reason the marks are.
     *
     * An expansion is given a scope of its own so that a body which defines
     * `@spin` may be invoked a hundred times without a redefinition, and so
     * that its locals are not visible to the caller. A body with no `@` in it
     * has neither to arrange: nothing it does can be seen by the scope
     * machinery and nothing the caller has done can be seen by it. Saving and
     * restoring the scope around one is 62 of the 417 instructions an
     * invocation costs. */
    bool haslocal;
};

/* What went wrong, as a code rather than a string.
 *
 * A code is what a caller other than `main` can branch on: zap is meant to be
 * usable as a library, and a library that reports faults by handing back
 * English is one nobody can act on. It is also a byte where a pointer is three,
 * and the store happens only after something has already failed, so it costs
 * nothing on the path that matters.
 *
 * The message text lives in one table beside the enum, keyed by code, rather
 * than at the hundred-odd places that detect a fault. */
typedef enum {
    ZAP_OK = 0,
    ZAP_E_ADL_0_OR_1,
    ZAP_E_FILLBYTE_COME_BEFORE_SPACE_FILLS,
    ZAP_E_IF_LEFT_OPEN_AT_END_FILE,
    ZAP_E_RELOCATE_DOES_NOT_NEST,
    ZAP_E_MACRO_WAS_NEVER_CLOSED,
    ZAP_E_LABEL_CANNOT_NEGATED,
    ZAP_E_LABEL_DEFINED_ALREADY,
    ZAP_E_MACRO_PARAMETER_NOT_NUMBER_OR_MNEM,
    ZAP_E_STRING_DB,
    ZAP_E_UNARY_OPERATOR_VALUE,
    ZAP_E_ADDRESS_OUTSIDE_16_BIT_RANGE,
    ZAP_E_ADDRESS_OUTSIDE_24_BIT_RANGE,
    ZAP_E_ALIGN_POSITIVE_NUMBER,
    ZAP_E_ALIGN_POWER_TWO,
    ZAP_E_IF_WAS_NEVER_CLOSED,
    ZAP_E_BAD_ESCAPE_IN_STRING,
    ZAP_E_BLK_POSITIVE_NUMBER,
    ZAP_E_CANNOT_OPEN_SOURCE,
    ZAP_E_CANNOT_OPEN_FILE,
    ZAP_E_CANNOT_READ_FILE,
    ZAP_E_CANNOT_REOPEN_FILE,
    ZAP_E_CANNOT_SET_FILE_ASIDE,
    ZAP_E_CONDITIONALS_DO_NOT_NEST,
    ZAP_E_DIVISION_BY_ZERO,
    ZAP_E_DS_POSITIVE_NUMBER,
    ZAP_E_EXPECTED,
    ZAP_E_EXPECTED_OR,
    ZAP_E_EXPECTED_AFTER_ADL,
    ZAP_E_EXPECTED_ADL,
    ZAP_E_EXPECTEDX,
    ZAP_E_EXPECTED_CHARACTER,
    ZAP_E_EXPECTED_FILE_NAME,
    ZAP_E_EXPECTED_MACRO_NAME,
    ZAP_E_EXPECTED_QUOTED_FILE_NAME,
    ZAP_E_EXPECTED_VALUE,
    ZAP_E_EXPECTED_INSTRUCTION,
    ZAP_E_EXPRESSION_NESTED_TOO_DEEPLY,
    ZAP_E_FILE_NAME_TOO_LONG,
    ZAP_E_INCLUDES_NESTED_TOO_DEEPLY,
    ZAP_E_INDEX_OFFSET_OUT_RANGE,
    ZAP_E_INTERRUPT_MODE,
    ZAP_E_INVALID_BIT_NUMBER,
    ZAP_E_INVALID_LABEL,
    ZAP_E_LABEL_DEFINED_TWICE,
    ZAP_E_LABEL_TOO_LONG,
    ZAP_E_LINE_TOO_LONG,
    ZAP_E_MACRO_NAME_TOO_LONG,
    ZAP_E_MACRO_PARAMETER_NAME_TOO_LONG,
    ZAP_E_MACROS_DO_NOT_NEST,
    ZAP_E_MACROS_NESTED_TOO_DEEPLY,
    ZAP_E_NO_ADL_MODE_CPU,
    ZAP_E_NO_IF_OPEN,
    ZAP_E_NO_MACRO_OPEN,
    ZAP_E_NO_RELOCATE_OPEN,
    ZAP_E_NO_ANONYMOUS_LABEL_ABOVE_ONE,
    ZAP_E_NO_ANONYMOUS_LABELS_ALLOWED_IN_MAC,
    ZAP_E_NO_GLOBAL_LABELS_ALLOWED_IN_MACRO,
    ZAP_E_NO_MODE_SUFFIX_CPU,
    ZAP_E_NO_SUCH_INSTRUCTION_FORM,
    ZAP_E_ORG_GOES_BACKWARDS,
    ZAP_E_OUT_MEMORY,
    ZAP_E_OUT_MEMORY_LABELS,
    ZAP_E_OUT_MEMORY_MACROS,
    ZAP_E_OUT_MEMORY_OUTPUT,
    ZAP_E_RELATIVE_JUMP_TOO_FAR,
    ZAP_E_RESTART_ADDRESS,
    ZAP_E_STRING_NOT_TERMINATED,
    ZAP_E_CONSTANT_TOO_LARGE_ADD_LABEL,
    ZAP_E_MACRO_ALREADY_DEFINED,
    ZAP_E_INSTRUCTION_NO_MODE_SUFFIX,
    ZAP_E_TOO_MANY_MACRO_ARGUMENTS,
    ZAP_E_UNEXPECTED_TEXT_AFTER_INSTRUCTION,
    ZAP_E_UNKNOWN_INSTRUCTION,
    ZAP_E_UNKNOWN_LABEL,
    ZAP_E_UNSUPPORTED_CPU_TYPE,
    ZAP_E_WRONG_NUMBER_MACRO_ARGUMENTS,

    /* Not a code: the number of them, so the table below cannot be short. */
    ZAP_E_COUNT
} zap_err;

/* What assemble_line does with a line before looking at it. */
#define LINE_ASSEMBLE 0

#define LINE_SKIP     1   /* a conditional is switched off */

#define LINE_CAPTURE  2   /* copying it into a macro being defined */

typedef struct symblock symblock;

/* Names live in blocks of this size; see namblock below for why. Declared here
 * because `zap_state` holds the list. */
#define NAMES_BLOCK 4096

typedef struct _namblock namblock;

struct _namblock {
    namblock* next;
    char buf[NAMES_BLOCK];
};

_Static_assert(NAMES_BLOCK > 255, "any single name has to fit in one block");

struct sym {
    const sym* next;

    /* The name, as a pointer rather than an offset into the arena. The arena
     * is a list of blocks that never move -- see namblock -- so a pointer
     * stays valid, and it saves an add in the compare loop, which is the
     * hottest loop the symbol table has. */
    const char* name;
    uint8_t len;

    /* A name is interned on first sight, defined or not, so a reference to a
     * label that has not appeared yet still gets an entry, and a fixup points
     * at that entry rather than carrying its own copy of the name.
     *
     * One copy per distinct name rather than one per reference is the
     * difference between fitting in 512 KB and not: a real program has two
     * references for every definition. Interning also means there is no lookup
     * when the reference is resolved -- the address is simply there. */
    bool defined;

    /* Which table this node came from, and so which arena its name is in and
     * which fixup list a reference to it belongs on. Kept on the node rather
     * than on the operand because the operand is copied twice a line with an
     * ldir and this is written once per distinct label. */
    bool islocal;

    /* The value: an address for an ordinary label, whatever was written for an
     * EQU. Four bytes, not the machine's three, because the reference keeps
     * four -- `X: equ 0x55555555` then `dw32 X` is 55555555 there, and `dl X`
     * is 555555 with a truncation warning. Truncation happens at the emitter,
     * on the width the directive asked for, not when the name is defined.
     *
     * Narrowing this would also make the Agon and the host disagree about any
     * EQU above 24 bits, since `int` is three bytes on one and four on the
     * other. */
    evalue addr;
};

/* How many buckets the symbol table has: 2,048, which is 8 KB of pointers.
 *
 * A larger table shortens chains a little and costs four times the memory,
 * which on a 512 KB machine is the wrong trade. */
#define NSYMB 2048

/* An expression that named something not yet defined and could not be reduced
 * to the symbols a fixup carries. See defer_expr. */
typedef struct {
    sym* sp;            /* the nameless symbol standing in for its value */
    const char* text;
    int len;
    int line;
} defexpr;

/* A block of n units whose fill named something not yet defined. The bytes
 * are written where they belong and the value is put in afterwards. */
typedef struct {
    const sym* sp;
    int off;
    int count;
    uint8_t width;
    int line;
} fillpatch;

/* A saved bucket, so an expansion can take the table over and give it back.
 * See scope_push. */
/* How deep INCLUDE and macro expansion may nest. Declared here because the
 * per-level expansion buffers are part of zap_state; the reasoning for the number is
 * where INCLUDE is. */
/* How deep INCLUDE and macro expansion may nest -- shared, because an
 * expansion is read the same way an included file is and re-enters the same
 * loop. Declared up here because the per-level expansion buffers are fields
 * of zap_state. */
#define INCLUDE_MAXDEPTH 8

#define UNDO_STEP 32

typedef struct {
    sym* head;
    uint8_t b;
    uint8_t gen;
} locundo;

typedef struct {
    sym* head;
    uint8_t pad;        /* see bucketslot: a power of two is a shift */
} symslot;

_Static_assert((sizeof(symslot) & (sizeof(symslot) - 1)) == 0,
               "symbol slot size is a power of two, so indexing is a shift");

_Static_assert(sizeof(symslot) > sizeof(sym*),
               "the pad is what makes the size a power of two");

/* Which of the two bucket keys to use. Build with -DZAP_SYMHASH=0 for the
 * other one; both index the same 2,048 buckets.
 *
 * The default is a Pearson hash. The alternative is a structural key -- first
 * character, last character and length -- which reads fewer characters per
 * lookup, because the scan that found the token has already walked the name.
 *
 * The hash wins because assembly labels cluster. Names like `lbl_0001` upward
 * share their first character, their last character and their length, so the
 * structural key puts hundreds of them in one bucket and its lookups degrade
 * by a factor of three. The hash reads every character but spreads them.
 *
 * The structural key is kept, compilable and correct, for a source whose
 * names are known to be well spread. */
#ifndef ZAP_SYMHASH
#define ZAP_SYMHASH 1
#endif

/* A reference to a label that is not defined yet.
 *
 * One pass means a forward reference cannot be resolved where it is read. The
 * output is held in memory in full, so the bytes are written as zeroes, a
 * fixup records where they are, and they are patched once the label settles --
 * at the end of the scope for a local, at the end of the source for a global.
 */
typedef struct {
    const sym* target;  /* interned, so no name and no lookup to do */

    /* A second symbol, added or subtracted, or NULL. `end - start` with both
     * labels still ahead is how a program measures a table it has not finished
     * writing, and it is the common shape among expressions that hold more
     * than one forward reference.
     *
     * It sits next to `target` rather than after `addend` so that the record
     * is a power of two on the host as well, where a pointer is eight bytes
     * and an int four. The static assert below is checked in both builds and
     * only this field order satisfies both. */
    const sym* sub;

    /* What to add to the address once it is known. `later + 4` is one symbol
     * and one constant, which is what an expression over a forward label comes
     * to when the label appears once and only `+` and `-` connect it -- the
     * common shape by a wide margin. Anything else is refused, because a fixup
     * with one addend cannot represent it.
     *
     * Three bytes on the Agon, and the only quantity on the expression path
     * that is not four. The record is sixteen bytes and every one of them is
     * spoken for -- see the assert below -- so widening this would cost a
     * multiply on every index into the list to buy `dw32 later + 0x55555555`,
     * which nobody writes. It is *checked* rather than truncated: an addend
     * outside the machine word is refused by fix_add, against the same
     * constants on the host and on the Agon, so the two cannot disagree.
     *
     * The symbol's own value is four bytes and is where a wide EQU lives. */
    int addend;

    uint8_t width;      /* 1, 2 or 3 bytes, or 0 for a relative displacement,
                         * plus FIX_SUB2 when `sub` is subtracted rather than
                         * added */
    int off;            /* where in the output it goes */
    int line;           /* to report against, long after the line is gone */
} fixup;

#define FIX_SUB2  0x80

#define FIX_WIDTH 0x7F

/* Widths 1, 2, 3 and 4 are byte counts and 0 is a relative displacement.
 * Above those are the folds: an operand that goes into the *opcode* rather
 * than after it, and could not be folded when the instruction was emitted
 * because the label was still ahead.
 *
 * `bit n, a` with `n` an EQU further down the file is the case. The bit number
 * lives in three bits of the opcode byte, so the fixup has to come back to
 * that byte rather than to the bytes after it. */
#define FIX_FOLD_BIT 5   /* (v & 7) << 3 into the opcode */

#define FIX_FOLD_RST 6   /* v into the opcode */

#define FIX_FOLD_IM  7   /* 0, 1, 2 as y = 0, 2, 3, shifted into the opcode */

/* The range an addend has to fit, written out rather than derived from `int`,
 * which is three bytes on the Agon and four on the host. Deriving it would
 * make this refuse on one machine and accept on the other.
 *
 * The constants are typed, and that is not decoration: an `evalue` compared
 * against a bare `int` constant tests the wrong part of the value on the Agon
 * while the host build is correct. Both sides of every comparison against an
 * evalue must be the same width. */
#define ADDEND_MIN ((evalue) -0x800000L)

#define ADDEND_MAX ((evalue)  0x7FFFFFL)

/* Sixteen bytes, and the size is the point: `&list[i]` on a record whose size
 * is not a power of two is a call to __imulu, because the eZ80's multiply is
 * 8-bit. At sixteen it is a shift. */
_Static_assert((sizeof(fixup) & (sizeof(fixup) - 1)) == 0,
               "fixup size is a power of two, so indexing it is a shift");

/* Local labels -- `@name` -- live in their own table, emptied at the end of
 * every scope rather than accumulating for the whole program.
 *
 * The reference keys a local as the enclosing global label's name with the
 * local's appended: `outer:` then `@aa:` is one entry spelled `outer@aa`, and
 * its "Label already defined" message says so. That is one way to build it and
 * a poor one here -- every local costs the scope's name again in the arena and
 * on every hash and every compare, and in isa_real a scope name averages
 * seventeen characters against three for `@aa`.
 *
 * The separate table falls out of the semantics instead. A local can only be
 * satisfied by a definition in its own scope -- the reference refuses `@aa`
 * defined before any global and used after one -- so when a scope ends every
 * local in it is finished with: resolved, or an error to report against the
 * line that used it. Nothing about it is needed afterwards, so the names, the
 * nodes and the pending references are all reused by the next scope, and a
 * program's local labels cost the high-water mark of one scope instead of the
 * sum of all of them.
 *
 * 64 nodes a block: the whole Agon corpus has at most 20 locals in one scope,
 * median 2 and 11 at the 99th percentile, over the 130 files of 1,000 that use
 * them at all. A scope needing more gets another block, and the blocks are
 * kept and reused rather than freed, so a program pays for its widest scope
 * once. */
#define NLOCB       64    /* local buckets; a power of two, see loc_bucket */

#define LOCS_STEP   64

#define LOCNAMES_STEP 256

typedef struct locblock locblock;

struct locblock {
    locblock* next;
    sym nodes[LOCS_STEP];
};

/* A bucket that empties in constant time.
 *
 * A scope ends at every global label, so clearing all 64 slots each time would
 * be thousands of stores for a table that usually holds two entries. Each slot
 * carries the number of the scope it belongs to instead: a slot whose stamp is
 * not the current one reads as empty however stale its chain, and ending a
 * scope is an increment.
 *
 * The stamp goes in a byte that is padding otherwise, so it costs nothing. */
typedef struct {
    sym* head;
    uint8_t gen;
} locslot;

_Static_assert((sizeof(locslot) & (sizeof(locslot) - 1)) == 0,
               "local slot size is a power of two, so indexing is a shift");

typedef struct _zap_state {
    buf_reader rd;

    /* The output, as three pointers rather than a base and two offsets.
     *
     * Reserving room is then a pointer compare against `lim`, which the
     * compiler does in one unsigned subtract. As a signed `pos + 12 > cap` it
     * would be eleven instructions and a `call pe, __setflag` to repair the
     * flags, once per instruction assembled. The emitter also takes and
     * returns its cursor directly, with nothing to add or subtract at either
     * end. */
    uint8_t* out;   /* the buffer, for realloc and for writing it out */
    uint8_t* o;     /* the next byte to write */
    uint8_t* lim;   /* the last address at which a whole instruction still fits */

    /* Where the first byte of the output goes, which `ORG` may move.
     *
     * Every address the assembler computes is `org + (o - out)`, so this is
     * read on every label definition, every `$`, every relative jump and every
     * fixup patched. It sits next to the cursor it is always added to rather
     * than among the cold fields at the end.
     *
     * `org_set` distinguishes the first ORG in a file from the rest: the first
     * relocates the origin, and every later one pads out to its address. The
     * reference draws the same distinction -- two ORGs with nothing between
     * them write the gap. */
    int org;
    bool org_set;

    /* A macro: its name, the names of its parameters, and the text between MACRO
     * and ENDMACRO.
     *
     * All three live in the name blocks, which never move, so a macro defined
     * early is still readable after any number of later ones. Macros are few --
     * the corpus's busiest file has eleven -- so the table is a list and the
     * lookup is a walk. It is only ever reached where a mnemonic lookup has
     * already failed. */
    macro* macros;

    /* What the line loop should do with the next line: assemble it, skip it
     * because a conditional is switched off, or copy it into the macro being
     * defined. One field and one branch per line, whichever it is. */
    uint8_t line_mode;

    /* The macro being defined, while line_mode says so. */
    macro* defining;
    /* Non-zero while a macro body is being expanded. The reference refuses a
     * global label in a body -- "No global labels allowed in macro definition"
     * -- and refuses it at the invocation, not at the definition, so a body
     * that is never invoked is never complained about. */
    uint8_t expanding;

    /* Conditional assembly, which is a flag and not a stack: the reference
     * says "Nested conditionals not supported" and means it, so one IF is all
     * there is to keep track of.
     *
     * `cond_emit` is what every line is tested against, and it is true whenever
     * there is no IF in force -- so the test is one field and one branch rather
     * than two. */
    bool in_cond;
    bool cond_emit;

    /* Whether an address-sized immediate is three bytes or two.
     *
     * `ASSUME ADL=0` is Z80 mode and makes `ld hl, 0x1234` three bytes rather
     * than four; `ADL=1` is the eZ80's own. A source may switch as often as it
     * likes and the reference honours it per line, so this is a field rather
     * than a constant. A forward reference records the width it had when the
     * instruction was emitted -- the fixup carries it -- so switching modes
     * between a reference and its definition changes nothing, as there. */
    bool adl;

    int cap;

    symslot* syms;      /* NSYMB buckets */
    namblock* names;    /* every label's text, copied, in blocks */
    int names_used;     /* within the newest block */
    symblock* blocks;   /* symbol nodes, in blocks that never move */
    int syms_used;      /* used in the newest block */
    fixup* fixups;
    int fix_used;
    int fix_cap;

    int line;
    zap_err err;

    /* Everything the report needs, written **only when a failure happens**.
     *
     * That is the whole discipline of it: not one of these is maintained in
     * advance, so a source that assembles pays nothing for the machinery that
     * would have described it failing. The line is copied rather than pointed
     * at, because a library caller may print after the reader that held it is
     * gone.
     *
     * The innermost capture wins: a macro body writes `errline` and the loop
     * that invoked it then finds it taken and writes `errfrom` instead, which
     * is how the two halves of "Invoked from" find their own line. */
    /* Where the line being listed started; see the line loop. */
    const uint8_t* lst_o;
    int lst_pc;
    /* The start of the line being assembled, and whether the listing for it
     * has already been written. A macro invocation writes its own -- the
     * invocation with no bytes on it, then the arguments, then a line per
     * body line, which is the order the reference prints them in and not the
     * order the line loop would produce. Both are set only when a listing is
     * being written at all. */
    const char* lst_p;
    bool lst_done;

    /* Where the listing has got to, and where the line being listed starts.
     *
     * A forward reference is emitted as zeroes and patched when its label
     * settles, long after its line was listed, so a listing written as the
     * assembly goes would show `ld hl, ahead` as 21 00 00 00. Every listed
     * line that leaves a fixup behind is remembered here, and its byte columns
     * are written again from the finished output before the listing file is
     * closed. See lstfix_apply. */
    int lst_pos;      /* bytes written to the listing file so far */
    int lst_lineat;   /* where the current line's first row began */
    int lst_row0;     /* how long that row was, through its newline */
    bool fix_touched; /* this line left a fixup behind */

    lstfix* lstfix;
    int lstfix_used;
    int lstfix_cap;

    bool errhave;          /* the failing line has been captured */
    char errline[ERRLINE_MAX];
    char errfrom[ERRLINE_MAX];   /* empty until the line loop fills it */
    const char* errfrompath;     /* NULL unless the failure was in a macro */
    int errfromline;
    const char* errfile;  /* the file, when the failure was inside a macro */
    const char* errmacro; /* the macro, or NULL */
    const char* errat;    /* the token to point at, or NULL */
    int erratlen;

    /* Anonymous labels: `@@`, which may be written any number of times and is
     * reached by position rather than by name -- `@b`/`@p` for the one above,
     * `@f`/`@n` for the one below.
     *
     * Backward needs nothing but the address of the last one seen. Forward is
     * a reference to a label that has not been written yet, which is what the
     * fixup list already exists for, so it is a symbol with no name and no
     * bucket: every `@f` between two `@@` points at the same one, and writing
     * the next `@@` defines it and starts another. An `@f` with no `@@` after
     * it is then an undefined symbol like any other, reported against the line
     * that used it. */
    int anon_prev;
    bool anon_has_prev;
    sym* anon_fwd;

    /* The line a global label was defined on, while the scope it opens has not
     * started yet; 0 when there is none pending.
     *
     * `two: jp @l` resolves `@l` against the scope `two` *closed*, not the one
     * it opens: a label and an instruction on one line are two things, and the
     * operand is read before the scope moves. The reference does the same, so
     * the scope change is held back until the end of the line. */
    int scope_line;

    /* The local table: buckets, the blocks the nodes come from, their own name
     * arena, and the references waiting on a definition in this scope. The
     * used counters are reset when a scope ends; the capacities are not.
     *
     * LAST IN THE STRUCT, DELIBERATELY. Where `zap_state` is reached through a
     * pointer, an `iy` displacement is a signed byte, so a field past 127 has
     * its address computed instead of being read in one instruction. These
     * buckets are 256 bytes on their own, and in the middle of the struct they
     * push hot fields like `line` -- written on every line of the source --
     * out of that range. Cold and bulky fields go at the end. */

    /* The file being read and how deep the includes go. `path` is what an
     * error message names, so it follows the reader down and back up again;
     * `depth` is what stops a file that includes itself from exhausting the
     * stack. Both are written once per file entered, so they belong down here
     * with the cold fields rather than among the ones a line touches. */
    const char* path;
    uint8_t depth;

    /* The run of reserved bytes the output currently ends with, if it ends
     * with one.
     *
     * `DS` and `ALIGN` reserve space rather than emit it, and the reference
     * materialises that space only when something is written after it: a file
     * ending in `DS 4` is four bytes shorter there, and a trailing `ALIGN`
     * emits nothing. `ORG` is different and does pad.
     *
     * Held as where the run ends and how long it is, rather than as a flag on
     * every write. Only `emit_fill` touches these, so nothing on the path an
     * instruction takes has to know they exist, and a trailing run is dropped
     * once, at the end. */
    int fill_end;
    int fill_len;
    /* What DS, ALIGN, ORG padding and a BLK with no fill of its own write.
     * 0xFF until FILLBYTE says otherwise, and it says so for the rest of the
     * assembly rather than for the next directive only. */
    uint8_t fill;
    /* The origin RELOCATE displaced, and whether one is open. Addresses are
     * `org + (o - out)` everywhere, so relocating is moving `org` and putting
     * it back -- labels, `$`, EQU and every fixup follow without knowing. */
    int reloc_org;
    bool reloc;
    /* Whether a reservation has been written yet. See FILLBYTE. */
    bool filled;

    /* Where `path` points when an include fails.
     *
     * The name of an included file lives in the frame of the include that
     * opened it. That is the right lifetime while the file is being read and
     * the wrong one afterwards: a failure unwinds those frames and then
     * reports, so `path` would name dead stack. The name is copied here on the
     * way out of a failure and nowhere else. The label arena cannot hold it --
     * that one is grown with realloc and moves. */
    char errpath[INCLUDE_NAME_MAX];

    locslot locs[NLOCB];
    locblock* locfirst;     /* kept, to rewind to */
    locblock* loccur;
    int locs_used;          /* in loccur */
    /* The same, for a scope's locals. `locnamfirst` is kept so that the end of
     * a scope can rewind to it rather than free and re-allocate: a scope ends
     * on every global label. */
    namblock* locnames;
    namblock* locnamfirst;
    int locnames_used;
    fixup* lfixups;
    int lfix_used;
    int lfix_cap;
    uint8_t gen;            /* which scope the local buckets belong to */
    locundo* undo;          /* buckets an expansion took over. scope_push */
    int undo_used;
    int undo_cap;
    /* Global fixups whose `sub` is a local, by index. See fix_add: the node a
     * local fixup points at stops meaning that label when the scope ends, and
     * a fixup that names a global *and* a local outlives the scope. Indices
     * rather than pointers, because the list they point into is realloc'd. */
    int* subfix;
    int subfix_used;
    int subfix_cap;
    /* Expressions that could not become a fixup, kept as text to be evaluated
     * when everything is known. See defer_expr. */
    defexpr* defer;
    int defer_used;
    int defer_cap;
    /* Blocks whose fill was not known when they were written. See the BLK
     * directives. */
    fillpatch* fillp;
    int fillp_used;
    int fillp_cap;
    /* One expansion buffer per level of nesting, kept and grown rather than
     * allocated per invocation, because a malloc and a free are a large part
     * of what an expansion costs. There is one per level because an outer
     * expansion is still being read from while an inner one is built; the
     * level in use is `depth`. */
    char* expbuf[INCLUDE_MAXDEPTH];
    int expcap[INCLUDE_MAXDEPTH];

    /* The arguments of the invocation being expanded at each depth. Here
     * rather than in a frame; see macro_args. */
    const char* margp[INCLUDE_MAXDEPTH * MACRO_MAXPARAM];
    int margn[INCLUDE_MAXDEPTH * MACRO_MAXPARAM];
} zap_state;

/* The fields touched on every line have to be reachable in one instruction.
 *
 * zap_state is reached through a pointer and `iy` displacement is a signed byte, so a
 * field past 127 has its address computed instead. `line` is written once per
 * line of the source and the output cursor is read and written several times,
 * which is why those three are named here rather than the struct being trusted
 * to stay small: the local table alone is 256 bytes of buckets, and putting it
 * anywhere but the end pushes `line` out of range. That is what this catches,
 * and it caught it. */
/* The rule itself, which holds on any machine: the 256 bytes of local buckets
 * come after every field that is touched per line, not before them. */
_Static_assert(__builtin_offsetof(zap_state, locs) > __builtin_offsetof(zap_state, line),
               "the local table must come after the per-line fields");

_Static_assert(__builtin_offsetof(zap_state, locs) > __builtin_offsetof(zap_state, lim),
               "the local table must come after the output cursor");

/* And the displacement itself, where a displacement is what it is. The host
 * has eight-byte pointers and a zap_state twice the size, so the number only means
 * anything on the machine this is for. */
#ifdef AGONDEV
_Static_assert(__builtin_offsetof(zap_state, line) < 128, "zap_state.line is out of range");
_Static_assert(__builtin_offsetof(zap_state, o) < 128, "zap_state.o is out of range");
_Static_assert(__builtin_offsetof(zap_state, lim) < 128, "zap_state.lim is out of range");
_Static_assert(__builtin_offsetof(zap_state, org) < 128, "zap_state.org is out of range");
#endif

/* ======================================================================
 * SYMBOLS: STORAGE
 *
 * Where the nodes and the names come from. Both are arenas of blocks that
 * never move, because a growing array cannot be reallocated on a machine
 * with 512 KB and no virtual memory.
 * ====================================================================== */

/* Grown in blocks rather than one allocation per label. A label is a few
 * bytes and there are thousands of them; malloc per label would cost more in
 * bookkeeping than the labels take. */
#define NAMES_STEP  (8 * 1024)

#define SYMS_STEP   512

#define FIX_STEP    512

/* Symbols are allocated in blocks that are never moved.
 *
 * A growing array would be simpler to write and needs every bucket chain
 * rebuilt whenever it moves -- and that rebuild cannot be tested: glibc
 * extends a growing block in place, so the array does not move, and deleting
 * the rebuild fails no check even with six hundred labels. Code that only runs
 * under an allocator that behaves differently is code nothing here can hold
 * to account.
 *
 * A block list has no such path. The chains are pointers and stay pointers,
 * and the only cost is one allocation per 512 labels. Names are an array
 * because they are addressed by offset, which does not care if it moves. */
struct symblock {
    symblock* next;
    sym nodes[SYMS_STEP];
};

/* Mnemonics bucketed by first letter.
 *
 * The instruction set is closed and small, so the first letter picks a bucket
 * of four or five and the rest is a length test and a compare. A general hash
 * over every reserved word would be about a thousand cycles a lookup on a
 * machine with no cache.
 */
/* Bucketed by first letter and length together.
 *
 * By first letter alone a bucket holds four or five candidates, and rejecting
 * one means measuring its length -- a strlen per candidate, to re-derive
 * something fixed when the table was written. Folding the length into the
 * bucket key leaves one or two candidates and no length to measure.
 */
/* Measuring what a table walk costs, without instrumenting the code.
 *
 * Each DUP_ flag below builds one of the tables with every entry duplicated,
 * so the walk over it does twice the work and nothing else changes. A matching
 * entry is still found at its first copy, so the output stays byte-identical
 * -- which is what says the measurement is valid, and the benchmark runner
 * prints the md5 to check it. The extra time is that walk's cost.
 *
 *   make EXTRA_CFLAGS=-DDUP_ROW      the register test in match_row
 *   make EXTRA_CFLAGS=-DDUP_GROUP    the mode group walk
 *   make EXTRA_CFLAGS=-DDUP_BUCKET   the mnemonic bucket chain
 *
 * test/run.sh builds all three and checks the bytes are unchanged, because a
 * flag that alters the output prices nothing.
 */
#ifdef DUP_ROW
#define DUP_ROW_N 2
#else
#define DUP_ROW_N 1
#endif

#define NLETTER 27

#define NLEN    8

#define NBUCKET (NLETTER * NLEN)

#ifdef DUP_ROW
#define NROW 644
#else
#define NROW 322
#endif

/* Mode groups across the whole table. 114 mnemonics, no mnemonic having more
 * than seven, and the four that are not grouped at all contributing none.
 * build_tables says so if the table outgrows it. */
#ifdef DUP_GROUP
#define NGRP 340
#else
#define NGRP 170
#endif

typedef struct rowinfo rowinfo;

struct rowinfo {
    /* Only the rows of the four ungrouped mnemonics are ever tested on this;
     * a grouped row reached through its group already agrees. */
    uint8_t modes;

    uint8_t ccok;
    uint8_t a0, a1, a2;
    uint8_t b0, b1, b2;
    uint8_t aempty, bempty;

    /* The row is held as a pointer rather than an index because the sort moves
     * it away from its position in the instruction's own table, and because
     * `&insn->rows[i]` is a multiply, which is a call. */
    const isa_row* row;
};

/* The rows of one mnemonic that share an operand mode.
 *
 * The modes live in a table of their own, so rejecting a mode is a compare and
 * a five-byte step rather than a turn of the row loop. The rows in a group
 * carry no mode test at all: being in the group is the answer.
 *
 * What makes a group contiguous is that the rows are sorted by mode, and that
 * is safe because the mode test is an equality -- rows outside the group can
 * never match, and the sort is stable, so the first matching row is the same
 * row the unsorted table would have found. */
typedef struct {
    uint8_t modes;
    uint8_t count;
    const rowinfo* rows;
} grpinfo;

/* One record per mnemonic, holding everything the hot path needs about it and
 * reached only by pointer.
 *
 * An index would have to be multiplied by a struct size at every use, and the
 * eZ80's multiply is 8-bit, so each of those is a call to __imulu. Chaining
 * the buckets through pointers and carrying the row block as a pointer leaves
 * exactly one subscript in the whole lookup: the bucket head. */
typedef struct insninfo insninfo;

struct insninfo {
    const insninfo* next;   /* next candidate in the same bucket */
    const char* name;

    /* The mode groups, and how many. Zero groups means the mnemonic is one of
     * the four whose rows are not grouped, and rows/count are the list to
     * scan instead. */
    const grpinfo* groups;
    uint8_t ngroups;

    const rowinfo* rows;
    uint8_t len;
    uint8_t count;
};

/* A bucket head, padded so that the array's element size is a power of two.
 *
 * The pad byte is the whole point. `bucket_head[b]` on a bare array of
 * pointers is b times three, and three is a call to __imulu -- MLT is 8-bit
 * and this is an int, and the compiler will not strength-reduce it or use MLT
 * for a 24-bit operand. Every portable way of writing the subscript keeps the
 * call; what removes it is the size, because a power of two is a shift.
 *
 * Four bytes here and sixteen on the host, both powers of two, so the
 * assertion holds either way and is what stops a field being added without
 * noticing that the multiply came back. 216 slots, so this costs 216 bytes. */
typedef struct {
    const insninfo* head;
    uint8_t pad;
} bucketslot;

_Static_assert((sizeof(bucketslot) & (sizeof(bucketslot) - 1)) == 0,
               "bucket slot size is a power of two, so indexing is a shift");

/* The one above passes on the host with the pad deleted, because a bare
 * pointer is eight bytes there and eight is a power of two. This is the same
 * intent stated so that the host can see it break. */
_Static_assert(sizeof(bucketslot) > sizeof(const insninfo*),
               "the pad is what makes the size a power of two");

/* Set once, from the command line, and read in the operator loop. A file-scope
 * flag rather than a field on zap_state, because zap_state is reached through a pointer on
 * every line and this is read only where an expression has an operator in it. */
/* Printed by -v. One place, so a release cannot say two things. */
#define ZAP_VERSION "1.0.0"

/* ======================================================================
 * REGISTERS AND CONDITION CODES
 *
 * Recognising `hl`, `(ix+d)`, `nz` and the rest straight from the text,
 * with the bits and indices the encoder wants.
 * ====================================================================== */

/* Recognised straight from the text, with the bit and the index the encoder
 * wants. zap reaches these through a token type and then a switch; there is no
 * token here to carry one. */
/* Writes the register's bytes straight into the operand.
 *
 * Handing back a 24-bit mask for the caller to split would put the shifts
 * after the switch, where the value is no longer a constant and `bit >> 16` is
 * a call to __ishru -- on every register operand in the source. Written inside
 * each arm, the shifts are constant expressions and fold away. */
#define SETREG(bits, idx)                        \
    do {                                         \
        op->r0 = (uint8_t) (bits);               \
        op->r1 = (uint8_t) ((bits) >> 8);        \
        op->r2 = (uint8_t) ((bits) >> 16);       \
        op->noreg = ((bits) == 0);               \
        op->reg_index = (idx);                   \
    } while (0)

/* ======================================================================
 * SCANNING
 *
 * The character class table every scan uses, the literal readers, and the
 * escape handling shared by strings and character literals.
 * ====================================================================== */

/* What each byte can be, in one table.
 *
 * Asking with a chain of comparisons costs three or four of them per
 * character, and every character of the source goes through at least one of
 * these questions. An indexed load answers all of them at once, and on this
 * chip a 256-byte table is reached in a single instruction. */
#define C_SPACE 0x01

#define C_NAME  0x02

#define C_DIGIT 0x04

#define C_NUM   0x08

#define C_MNEM  0x10

/* A name character that is not a digit -- what a register or flag has to start
 * with. Its own bit in the class table rather than
 * `name_ch(c) && !digit_ch(c)`, which would load the same class byte twice and
 * mask it twice on every operand in the source. */
#define C_ALPHA 0x20

/* The two characters that decide what kind of operand this is, so that one
 * class load answers the question instead of four compares.
 *
 * C_OPEND is what ends an operand list -- a comma, a newline, or the start of
 * a remark. C_LPAREN is the open paren that begins an indirect operand. With
 * C_ALPHA these three are the whole of the decision, and they now come out of
 * a single byte. */
#define C_OPEND 0x40

#define C_LPAREN 0x80

/* The expression evaluator.
 *
 * Under `-ez80` it must fold strictly left to right with no precedence, which
 * is what the reference does: `1+2*3` is 9 there. See binding_power.
 *
 * Very few operands hold an expression at all, and most of those have exactly
 * one operator, so the shapes to be fast on are `label+1` and `end-start`
 * rather than anything that needs a stack.
 *
 * Grouping is `[...]`, not parentheses, because parentheses already mean
 * indirection. Terms may be a number, a label, `$` for the address of the
 * instruction being assembled, a character literal, or a bracketed expression,
 * each optionally preceded by unary `+`, `-` or `~`.
 *
 * Out of line, and deliberately: it is reached by one operand in twenty-nine,
 * and assemble_line has no registers to spare for the other twenty-eight. */

/* Bracket nesting and precedence levels both recurse, so both are bounded.
 * The reference has no limit and a deep enough file takes the stack out from
 * under it; on a machine with 512 KB and no memory protection that is a reboot
 * rather than a message. Seven precedence levels and this much nesting is more
 * than any real source and cheaper than finding out. */
#define EXPR_MAXDEPTH 32

/* Every scan below is bounded, and the reader also keeps a newline one byte
 * past the last valid one.
 *
 * The two do different jobs. The sentinel is what makes a scan *terminate*:
 * no character class contains a newline, so every loop stops on it whether or
 * not it tests the end. The bound is what stops the compiler rotating the loop
 * -- see the header of this file -- which the sentinel cannot help with,
 * because a rotated loop does not run off the end, it skips the first
 * character.
 *
 * Single tests like `*p == ','` have no bound and need none: they are not
 * loops, so there is nothing to rotate, and the sentinel is what makes reading
 * one character past the content safe. */
/* Where a truncated stage sinks what it computed, so the compiler cannot
 * delete the work whose result nothing reads. Declared here because both the
 * line-level and the operand-level cuts write to it, and parse_operand comes
 * first in the file. */


/* The same trick one level down: -DPTRUNC=n stops parse_operand part way, and
 * is built with -DTRUNC=5 so that assemble_line stops after the operands.
 *
 *   1  the operand is cleared and its first character classified
 *   2  + the register path, where one starts with a letter
 *   3  + the literal or the expression, which is the whole of it
 *
 * A truncated operand is left as the empty template, which is why this only
 * makes sense with the row selection switched off: match_row would be reading
 * an operand nobody filled in. */
#ifdef PTRUNC
#define PTRUNC_AT(n)                         \
    do {                                     \
        if ((PTRUNC) <= (n)) {               \
            trunc_sink = (int) (op->mode);   \
            *pp = p;                         \
            return true;                     \
        }                                    \
    } while (0)
#else
#define PTRUNC_AT(n) do { } while (0)
#endif

/* The local-label scope a macro expansion runs in, set aside and put back.
 *
 * An expansion needs a scope of its own, as it has in the reference: a body
 * defining `@loc` may be invoked twice, and a body naming `@a` does not see
 * the caller's. The caller's scope has to survive, because a local defined
 * before an invocation may be named after it.
 *
 * Entering the scope is advancing the generation stamp, which is what ends an
 * ordinary scope too: every bucket then belongs to an older generation and
 * reads as empty. Leaving it is the stamp going back.
 *
 * What the stamp cannot undo is a bucket the body *wrote*, since claiming a
 * bucket overwrites the head the caller had. Those are recorded as they happen
 * -- see undo_note -- and there are only ever as many as the body has distinct
 * local names. Copying all 64 buckets out and back instead would cost more per
 * invocation than the whole expansion.
 *
 * The slot and name arenas are shared with the caller rather than replaced.
 * `scope_end` rewinding them onto the caller's live slots cannot arise: a
 * scope ends only at a global label or an EQU, and both are refused inside a
 * macro body, here as in the reference. The body appends, and the counters
 * rewind at the end to hand the space back. */
typedef struct {
    locblock* loccur;
    namblock* locnames;
    int locs_used;
    int locnames_used;
    int lfix_used;
    int subfix_used;
    int undo_used;
    int scope_line;
    uint8_t gen;
} locsave;

/* Which directive a token is, or none.
 *
 * Reached only when mnemonic_of has already failed, so an ordinary instruction
 * line never runs a character of it. That is why it is spelled out as compares
 * rather than bucketed the way mnemonics are: on this path a switch on length
 * costs nothing, and a table would be memory every program pays for.
 *
 * A leading dot is optional on all of them -- `.DB`, `.ALIGN` -- as in the
 * reference. `WORD`, `DWORD`, `DEFL`, `DC`, `TEXT` and `DB8` are *not*
 * directives there, however plausible they look. */
#define DIR_NONE  0

#define DIR_DB    1   /* one byte per value, and strings */

#define DIR_DW    2   /* two */

#define DIR_DL    3   /* three; the eZ80 word */

#define DIR_DW32  4   /* four, which is wider than the machine */

/* ASCIZ is the data list plus one terminating zero, so it is not a width and
 * cannot live in the range above. */
#define DIR_ASCIZ 5

#define DIR_DS    6   /* reserve, filled with FILLBYTE's value */

#define DIR_ALIGN 7

#define DIR_ORG   8

#define DIR_FILLBYTE 9

#define DIR_RELOCATE 10

/* BLKB, BLKW, BLKP and BLKL: n units of a fill value, written out. Consecutive
 * and in width order, so the width is `kind - DIR_BLKB + 1`.
 *
 * Not the same directive as DS, and the difference matters twice: DS reserves
 * space filled with the FILLBYTE and ignores any fill argument, and a run of
 * it at the end of a file is dropped, while BLK writes the fill it is given
 * and is never dropped.
 *
 * BLKL is four bytes wide, which is why the evaluator has to be: sources fill
 * it with values like `0x55555555` that a 24-bit evaluator cannot hold. */
#define DIR_BLKB  11

#define DIR_BLKW  12

#define DIR_BLKP  13

#define DIR_BLKL  14

/* Below INCLUDE, because these three are tested by name and the file
 * directives are tested as a range. */
#define DIR_CPU         15

#define DIR_ENDRELOCATE 16

#define DIR_ASSUME      17

#define DIR_INCLUDE 18

#define DIR_INCBIN  19

/* MACRO and ENDMACRO sit below the conditionals, and the ordering is
 * load-bearing: a line inside a switched-off branch asks `kind >= DIR_IF` and
 * nothing else, so everything at or above IF is still handled while skipping
 * and everything below it is skipped. With MACRO above them,
 * `IF 0 / MACRO m / ... / ENDMACRO / ENDIF` would capture the body and define
 * the macro, which the reference does not. */
#define DIR_MACRO    20

#define DIR_ENDMACRO 21

#define DIR_IF       22

#define DIR_ELSE     23

#define DIR_ENDIF    24

/* ======================================================================
 * DIRECTIVES
 *
 * Everything that is not an instruction: the data and space directives,
 * ORG and ALIGN, INCLUDE and INCBIN, the conditionals, ASSUME and .CPU.
 * ====================================================================== */


/* How deep INCLUDE may go, and how much buffer a file below the first gets.
 *
 * The top-level file keeps BUF_KB, because that is what sets the longest line
 * a source may have. An included file gets less: four kilobytes is still far
 * more than any real line, and eight of them is 32 KB rather than 128. */
#define INCLUDE_BUF_KB   4

typedef struct _emitted {
    uint8_t prefix1;
    uint8_t prefix2;
    uint8_t opcode;
} emitted;

/* Register-set masks by byte plane, so a test is one or two byte ANDs rather
 * than a 24-bit AND -- which would be a call to __iand and another to
 * __lcmpzero. The assertions tie these to the definitions in operand.h, which
 * is what stops the two drifting apart silently. */
#define RP1_IX  0xD0   /* (R_IX  | R_IXH | R_IXL) >> 8  */

#define RP1_IY  0x20   /* (R_IY  | R_IYH | R_IYL) >> 8  */

#define RP2_IY  0x03   /* (R_IY  | R_IYH | R_IYL) >> 16 */

#define RP1_XYL 0x80   /* (R_IXL | R_IYL)         >> 8  */

#define RP2_XYL 0x02   /* (R_IXL | R_IYL)         >> 16 */

_Static_assert((R_IX | R_IXH | R_IXL) == ((uint32_t) RP1_IX << 8), "IX plane");

_Static_assert((R_IY | R_IYH | R_IYL)
                   == (((uint32_t) RP2_IY << 16) | ((uint32_t) RP1_IY << 8)),
               "IY planes");

_Static_assert((R_IXL | R_IYL)
                   == (((uint32_t) RP2_XYL << 16) | ((uint32_t) RP1_XYL << 8)),
               "IXL/IYL planes");

/* Folds an operand into the opcode, and returns false if it does not fit the
 * field it folds into: a bit number above 7, an interrupt mode above 2, an
 * address that is not one of the eight restarts. Masking them instead would
 * assemble an instruction the source did not write -- `bit 8, a` as
 * `bit 0, a`, `rst 0x09` as `rst 0x08` -- and the reference refuses all three.
 *
 * The checks sit here rather than in the matcher because this is where the
 * field is known: `IMM_BIT` is a marker on the row, and the row is not chosen
 * until the operands are parsed. Failing the match instead would report "no
 * such instruction form", which is true and useless. */
/* What transform did with the operand. */
#define TRF_OK    0

#define TRF_ERR   1

#define TRF_DEFER 2

/* And one level down inside the label path, built with -DTRUNC=3.
 *
 *   1  the colon is found, and nothing else
 *   2  + the label is defined, whichever of the three kinds it is
 *   3  + EQU, which is the whole of it
 *
 * Safe to cut because a definition is only ever read by an operand, and stage
 * 3 has the operands switched off. */
#ifdef LTRUNC
#define LTRUNC_AT(n)                         \
    do {                                     \
        if ((LTRUNC) <= (n)) {               \
            trunc_sink = (int) n;            \
            goto trunc_done;                 \
        }                                    \
    } while (0)
#else
#define LTRUNC_AT(n) do { } while (0)
#endif

/* The same for the emitter, built with -DTRUNC=7, which is the ordinary build.
 *
 *   1  room is reserved for the instruction
 *   2  + the prefixes are chosen and the operands folded into the opcode
 *   3  + the prefix, opcode and displacement bytes are written
 *   4  + the immediate or the relative, and the fixup it may need
 *
 * The output is wrong in the first two, which is the point, and `state.o` does not
 * advance -- safe here only because these two sources contain no relative jump
 * whose reach depends on it. */
#ifdef ETRUNC
#define ETRUNC_AT(n)                         \
    do {                                     \
        if ((ETRUNC) <= (n)) {               \
            trunc_sink = (int) n;            \
            return true;                     \
        }                                    \
    } while (0)
#else
#define ETRUNC_AT(n) do { } while (0)
#endif

/* ======================================================================
 * THE LINE LOOP, REPORTING, AND MAIN
 *
 * assemble_line and the loop that feeds it, then everything that happens
 * once the source has run out: patching fixups, writing the output, the
 * listing, the symbol file, and the command line.
 * ====================================================================== */

/* Assembles one line and reports where it stopped.
 *
 * The end of the line is not found first. Scanning to the newline to bound
 * this function would mean two passes over every byte of the source to parse
 * it once, and no scan needs that bound: a newline is not a space, not a name
 * character and not part of a number, so every loop stops on it anyway. The
 * caller is told where parsing ended and steps over the newline from there.
 *
 * The bound the scans do carry is `e`, the end of the *buffer*, which the
 * caller already has. That is a different thing from the newline and it is
 * there for a different reason -- see the header of this file. */
/* Whether a token would be read as a number rather than as a name.
 *
 * A label cannot be spelled like a literal: the reference refuses `a00h:`,
 * `ffh:`, `e5h:`, `ah:` and `1010b:` -- all of which are numbers with a radix
 * suffix -- while accepting `beef:`, `zzh:`, `h:` and `a0h_x:`, none of which
 * are. zap accepted every one of them, which is a divergence in the direction
 * that produces plausible bytes rather than an error: `ffh: nop` defined a
 * label the reference would have refused, and any later `ld a, ffh` then meant
 * something different in the two assemblers.
 *
 * Found by a benchmark generator that produced `a00h` by accident.
 *
 * The same tests the operand parser uses, in the same order, so the two cannot
 * disagree about what a number is. */

/* Where the time in a line goes, by building an assembler that stops part way.
 *
 * -DTRUNC=n keeps stages 1..n of assemble_line and skips the rest; the
 * difference between two builds is the stage between them. See
 * the optimization guide (section 5), which this exists to serve.
 *
 *   1  the line is read and found not to be blank or a remark
 *   2  + the mnemonic run is scanned
 *   3  + the label, if any, is defined -- and an EQU is a label line
 *   4  + mnemonic_of, or the directive that its failure dispatches
 *   5  + both operands are parsed
 *   6  + the row is chosen
 *   7  + the bytes are emitted, which is the ordinary build
 *
 * Two rules, both learned the hard way and both enforced here. Every value a
 * stage produces is sunk into a volatile, or the compiler deletes the work
 * whose result nothing reads and the stage measures nothing. And every variant
 * leaves the line the way the loop expects to find it -- the scan to the
 * newline at `trunc_done` is in *every* build including the seventh, so it is
 * a constant across the set and cancels out of the differences. A probe that
 * skipped it once left match_row reading uninitialised memory, and the guest
 * wandered off for 469 seconds before anything noticed.
 */
#ifdef TRUNC
#define TRUNC_AT(n, v)                       \
    do {                                     \
        trunc_sink = (int) (v);              \
        if ((TRUNC) <= (n)) {                \
            goto trunc_done;                 \
        }                                    \
    } while (0)
#else
#define TRUNC_AT(n, v) do { } while (0)
#endif

/* Defined in one translation unit, used from others. */
extern const char* const zap_err_text[];
void build_pearson(void);
extern zap_state state;
void err_line(char* dst, const char* p, const char* e);
extern bool want_warn;
void warn_imm(int v, int width);
void err_tok(const char* s, int n);
extern volatile int dup_hash_sink;
char* nam_take(namblock** head, int* used, int len);
bool sym_room(void);
sym* sym_intern(const char* name, int len);
bool patch_fixup(const fixup* f);
bool fold_subs(int from);
bool scope_end(void);
sym* loc_intern(const char* name, int len);
bool anon_define(int addr);
sym* anon_next(void);
bool fix_add(const sym* target, const sym* sub, int addend, uint8_t width, int off);
bool out_reserve(void);
extern volatile int trunc_sink;
extern bucketslot bucket_head[NBUCKET];
extern const uint8_t shl3[8];
extern const uint8_t shl4[16];
extern uint8_t exop[256];
extern uint8_t exprec[256];
extern int opt_org;
extern uint8_t opt_fill;
extern bool opt_adl;
extern bool compat_ez80;
extern bool use_color;
extern bool want_list;
extern bool want_console_list;
extern bool want_symbols;
extern bool want_stats;
extern bool listing;
extern uint8_t list_fh;
extern uint8_t cpu_mask;
extern uint8_t letter_base[256];
void build_tables(void);
bool same_full(const char* name, const char* s, int n);
bool same_ci_full(const char* name, const char* s, int n);
uint8_t suffix_code(uint8_t bit);
bool reg_of_text(const char* s, int n, dop* op, bool* is_cc, uint8_t* cc_index);
extern uint8_t cclass[256];
extern uint8_t hexval[256];
void build_cclass(void);
extern const dop dop_none;
bool numeric_token(const char* s, int n);
extern const sym* expr_fwd;
extern const sym* expr_fwd2;
extern bool expr_fwd_neg;
extern bool expr_fwd2_neg;
extern bool expr_fwd_bad;
uint8_t fwd_live(void);
sym* defer_text(const char* text, int n);
bool defer_expr(const char* text, int n, dop* op);
bool fwd_finish(dop* op);
void fwd_reset(const sym* seed);
bool expr_atom(evalue* out, const char* ns, int nn);
bool expr_climb(evalue* total, const char** pp, const char* e, uint8_t minprec, int depth, uint8_t* fwdmask);
bool expr_value(evalue* out, const char** pp, const char* e, uint8_t* fwdmask);
bool is_equ_at(const char* p);
bool equ_line(const char* name, int nlen, const char* p, const char* e, const char** stop);
bool line_fill(buf_reader* r);
bool scope_push(locsave* sv);
bool scope_pop(locsave* sv);
const macro* macro_at(const char* s, int n);
bool macro_begin(const char** pp, const char* e);
bool macro_line(const char* p, const char* e);
bool macro_expand(const macro* m, const char* p, const char* e, const char** stop);
uint8_t directive_of(const char* s, int n);
int str_escape(char c);
bool directive_line(const char* s, int n, const char* p, const char* e, const char** stop);
bool fold_defer(uint8_t type, const dop* op, uint8_t prefix1, uint8_t prefix2, uint8_t flags, int off);
uint8_t* emit_imm(uint8_t* o, const dop* op, uint8_t cond, bool adl);
bool macro_capture(const char* s, int n, const char* p, const char* e, const char** stop);
bool cond_skip(const char* s, int n, const char* p, const char* e, const char** stop);
const insninfo* suffixed_mnemonic(const char* s, int n, uint8_t* suffix);
bool suffixed_insn(const insninfo* insn, uint8_t suffix, const char* p, const char* e, const char** stop);
bool third_operand(const insninfo* insn, dop* a, dop* b, const char* p, const char* e, const char** stop);
bool assemble_line(const char* p, const char* e, const char** stop);
bool run_lines(void);
void list_line(int pc, const uint8_t* from, const uint8_t* to, int line, int depth, const char* text, const char* tend);
void lstfix_add(const uint8_t* from, const uint8_t* to);
void list_invocation(const macro* m, int base, int line, int depth, const char* e);
void list_args(const macro* m, int base, int depth);
void warn_trunc(evalue v, int width);
void warn_initializer(const char* t, int n);

/* Small and hot: declared here so every caller can inline them. */

/* Whether a value survives being written in `width` bytes.
 *
 * It fits if the bytes that come out mean the same number read either way, as
 * signed or as unsigned: `ld a, -1` and `ld a, 255` are both one byte and lose
 * nothing, while `ld a, 256` and `ld a, -129` are both a byte that says
 * something else. The reference draws the line in the same two places.
 *
 * Written as two casts rather than as a pair of range compares: signed
 * compares on the eZ80 cost a `call pe, __setflag` apiece to repair the
 * flags. */
static inline bool fits_width(evalue v, int width) {
    const uint32_t u = (uint32_t) v;
    if (width == 1) {
        return (u + 128u) <= 383u;
    }
    if (width == 2) {
        return (u + 32768u) <= 98303u;
    }
    if (width == 3) {
        return (u + 0x800000u) <= 0x17FFFFFu;
    }

    return true;
}

static inline sym* sym_define(const char* name, int len, int addr) {
    sym* sp = sym_intern(name, len);
    if (sp == NULL) {
        return NULL;
    }
    if (sp->defined) {
        state.err = ZAP_E_LABEL_DEFINED_TWICE;

        return NULL;
    }
    sp->defined = true;
    sp->addr = addr;

    return sp;
}

static inline sym* loc_define(const char* name, int len, int addr) {
    sym* sp = loc_intern(name, len);
    if (sp == NULL) {
        return NULL;
    }
    if (sp->defined) {
        state.err = ZAP_E_LABEL_DEFINED_TWICE;

        return NULL;
    }
    sp->defined = true;
    sp->addr = addr;

    return sp;
}

/* The same bucket, reached the way the hot path wants it.
 *
 * bucket_of clamps the length, and that clamp is a *signed* compare: eleven
 * instructions and a `call pe, __setflag` to fix the flags up on overflow, on
 * every line of the source. A token of eight characters or more is not a
 * mnemonic -- the longest is five -- so the clamp can be a rejection instead,
 * and unsigned it is one compare.
 *
 * build_tables keeps using bucket_of, where the cost does not matter and the
 * clamp rather than the rejection is what the table wants. */
static inline const insninfo* bucket_at(char first, unsigned n) {
    if (n >= NLEN) {
        return NULL;
    }

    return bucket_head[letter_base[(uint8_t) first] + n].head;
}

/* Two things this does not do, because the bucket has already done them.
 *
 * It starts at index 1: the bucket is chosen by letter_base[first], which maps
 * 'a' and 'A' to the same base, so every candidate in the chain already agrees
 * with the source on its first character.
 *
 * And it case-folds one side, not two. Every name in the table is lower case,
 * which is checked when the table is built, so only the source needs the OR. */
static inline bool same_ci(const char* name, const char* s, int n) {
    /* Unsigned, and that is not incidental: two signed ints compared with `<`
     * cannot be done in one subtract, so the compiler emits a
     * `call pe, __setflag` to repair the flags on overflow -- here, inside the
     * loop that compares a mnemonic on every line of the source. */
    for (unsigned i = 1; i < (unsigned) n; i++) {
        if (name[i] != (s[i] | 0x20)) {
            return false;
        }
    }

    return true;
}

static inline const insninfo* mnemonic_of(const char* s, int n) {
#ifdef MTRUNC
    /* The bucket and not the walk, so the two halves of the lookup can be told
     * apart. Built with -DTRUNC=4 -DTRUNC_NODIR, where nothing reads the
     * answer. */
    trunc_sink = bucket_at(s[0], (unsigned) n) != NULL;

    return NULL;
#endif
    for (const insninfo* ins = bucket_at(s[0], (unsigned) n); ins != NULL;
         ins = ins->next) {
        /* No length test. The bucket is keyed by first character *and*
         * length, and the clamp at NLEN is never reached because the longest
         * mnemonic is five characters -- checked when the table is built --
         * so every candidate in this chain already has the length wanted. A
         * token longer than the clamp lands in a bucket that holds nothing. */
        if (same_ci(ins->name, s, n)) {
            return ins;
        }
    }

    return NULL;
}

static inline bool is_space_ch(char c) {
    return (cclass[(uint8_t) c] & C_SPACE) != 0;
}

static inline bool name_ch(char c) {
    return (cclass[(uint8_t) c] & C_NAME) != 0;
}

static inline bool num_ch(char c) {
    return (cclass[(uint8_t) c] & C_NUM) != 0;
}

static inline bool alpha_ch(char c) {
    return (cclass[(uint8_t) c] & C_ALPHA) != 0;
}

static inline bool digit_ch(char c) {
    return (cclass[(uint8_t) c] & C_DIGIT) != 0;
}

/* A run of hexadecimal digits, assembled into a value.
 *
 * Read from the end, a byte at a time, rather than accumulated as
 * `acc = (acc << 4) | digit`. There is no barrel shifter here, and the
 * compiler will not turn a left shift into a byte move even at a byte
 * boundary: `<< 4` and `<< 8` are both `ld c, n; call __ishl`, a loop over the
 * bits, several hundred cycles per digit.
 *
 * Working backwards, two digits make a byte with one table lookup for the high
 * nibble, and the bytes go straight into the value's own storage. Nothing here
 * shifts anything wider than a nibble.
 *
 * Three fixed steps rather than a loop: a value is at most three bytes, so a
 * loop could only run three times and would pay for a counter, a bound and an
 * indexed store into the union.
 *
 * The digits are validated here rather than in a pass of their own. `hexval`
 * gives 0xFF for anything that is not a hex digit and a real nibble is 0x0F or
 * less, so OR-ing the nibbles together and testing the high half at the end
 * says whether any was rejected, with no branch per digit.
 *
 * Little-endian, which both the eZ80 and the host are; the emitter writes the
 * low byte first for the same reason. A run longer than the machine's word is
 * declined rather than truncated -- see the test.
 *
 * It takes the run of digits rather than a whole token, which is what lets
 * `0x1234` and `1234h` share it. */
static inline bool hex_digits(const char* d, int n, int* out) {
    /* Six digits, which is the machine's word. Anything longer is declined
     * here and read by num_parse, which works in the evaluator's wider word
     * and handles every radix.
     *
     * Keeping four bytes here so that `dw32 0x55555555` could come through
     * would widen the union and the variable it moves through for every
     * literal in the file, to serve a form that is rare. Declining costs
     * nothing: lit_value returns NULL and the caller falls through to the
     * evaluator.
     *
     * Six and not seven, because seven digits is 28 bits and does not fit
     * either. */
    if (n > 6) {
        return false;
    }

    union {
        int v;
        uint8_t b[sizeof(int)];
    } u;
    u.v = 0;

    uint8_t bad = 0;
    int j = n;

    if (j > 0) {
        uint8_t c = hexval[(uint8_t) d[--j]];
        bad |= c;
        if (j > 0) {
            const uint8_t hi = hexval[(uint8_t) d[--j]];
            bad |= hi;
            /* Masked: an invalid digit reaches this before `bad` is tested,
             * and shl4 holds sixteen entries. */
            c = (uint8_t) (c | shl4[hi & 15]);
        }
        u.b[0] = c;
    }
    if (j > 0) {
        uint8_t c = hexval[(uint8_t) d[--j]];
        bad |= c;
        if (j > 0) {
            const uint8_t hi = hexval[(uint8_t) d[--j]];
            bad |= hi;
            c = (uint8_t) (c | shl4[hi & 15]);
        }
        u.b[1] = c;
    }
    if (j > 0) {
        uint8_t c = hexval[(uint8_t) d[--j]];
        bad |= c;
        if (j > 0) {
            const uint8_t hi = hexval[(uint8_t) d[--j]];
            bad |= hi;
            c = (uint8_t) (c | shl4[hi & 15]);
        }
        u.b[2] = c;
    }

    if ((bad & 0xF0) != 0) {
        return false;
    }
    *out = u.v;

    return true;
}

static inline bool fwd_result(const sym** target, const sym** sub,
                              bool* subneg) {
    *sub = NULL;
    *subneg = false;
    if (expr_fwd_bad) {
        state.err = ZAP_E_LABEL_DEFINED_ALREADY;

        return false;
    }
    if (expr_fwd2 == NULL) {
        if (expr_fwd_neg) {
            state.err = ZAP_E_LABEL_CANNOT_NEGATED;

            return false;
        }
        *target = expr_fwd;

        return true;
    }
    if (!expr_fwd_neg) {
        *target = expr_fwd;
        *sub = expr_fwd2;
        *subneg = expr_fwd2_neg;
    } else if (!expr_fwd2_neg) {
        *target = expr_fwd2;
        *sub = expr_fwd;
        *subneg = true;
    } else {
        /* Both subtracted. A fixup adds its first symbol, so there is nowhere
         * for `-a - b` to go. */
        state.err = ZAP_E_LABEL_CANNOT_NEGATED;

        return false;
    }

    return true;
}

__attribute__((always_inline)) static inline bool parse_operand(dop* op, const char** pp, const char* e) {
    *op = dop_none;

    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }

    /* One class load decides what this operand is: whether the operand list
     * has ended, whether a parenthesis opens it, and whether a register starts
     * here are all one byte's worth of information about one character.
     *
     * The end of the operand list is checked here rather than by bounding the
     * scan at the end of the line, because finding that end would mean another
     * pass over the source. */
    uint8_t cl = cclass[(uint8_t) *p];

    if ((cl & C_OPEND) != 0) {
        *pp = p;

        return true;   /* nothing there */
    }

    if ((cl & C_LPAREN) != 0) {
        op->mode |= INDIRECT;
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        cl = cclass[(uint8_t) *p];
    }
    PTRUNC_AT(1);

    /* A register or flag? */
    const char* known_end = NULL;
    if ((cl & C_ALPHA) != 0) {
        const char* s = p;
        while (p < e && name_ch(*p)) {
            p++;
        }
        const char* const nend = p;
        /* The shadow accumulator is a register whose name ends in a character
         * no other token may contain, so it is taken here rather than given a
         * class of its own. */
        if (*p == '\'') {
            p++;
        }
        const int n = (int) (p - s);

        bool is_cc = false;
        uint8_t cc_index = 0;
        if (reg_of_text(s, n, op, &is_cc, &cc_index)) {
            if (is_cc) {
                op->cc = true;
                op->cc_index = cc_index;
                if ((op->r0 | op->r1 | op->r2) == 0) {
                    /* A flag written as one, which a row asks for with CC. */
                    op->mode |= CC;
                }
            }

            /* (ix+d) and (ix-d). */
            while (p < e && is_space_ch(*p)) {
                p++;
            }
            /* Not only inside parentheses. `lea bc, ix+5` and `pea ix+5`
             * take a displacement on a bare register -- their rows ask for
             * NOREQ with F_DISPA or F_DISPB, not INDIRECT -- and requiring
             * the parenthesis here is why twelve forms of the reference's own
             * corpus did not assemble. Row selection rejects the combinations
             * that are not real, so nothing else has to. */
            if (*p == '+' || *p == '-') {
                const bool neg = *p == '-';
                p++;
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
                const char* ds = p;
                /* Bounded, like every character scan here. Without the bound
                 * this loop is compiled rotated: it never examines the first
                 * character and stops one short, so `ld a, 0x42` parses as
                 * 0x4 and leaves `2` behind. Indexing from a base instead of
                 * advancing a pointer does not avoid it. See the header of
                 * this file. */
                while (p < e && num_ch(*p)) {
                    p++;
                }
                /* A displacement is one signed byte by the time it is
                 * written, so it is accumulated in the machine's word rather
                 * than the evaluator's 32-bit one. */
                /* The first digit is taken outside the loop, so a
                 * one-digit displacement needs no multiply at all -- and
                 * almost every displacement is one digit. `d * 10` is a call
                 * to __imulu, because the eZ80's multiply is 8-bit and this is
                 * an int; leaving it in the loop meant paying that call even
                 * for `(ix+8)`, where the accumulator is still zero. */
                int d = 0;
                bool got = false;
                if (digit_ch(*ds)) {
                    const char* q = ds + 1;
                    d = *ds - '0';
                    while (q < p && digit_ch(*q)) {
                        d = d * 10 + (*q - '0');
                        q++;
                    }
                    got = q == p;
                }
                if (!got) {
                    /* Not a plain decimal, so the general parser takes it.
                     *
                     * The fast path must hand over rather than refuse: `05h`
                     * and `0x05` both begin with a digit, so the decimal scan
                     * claims them and then stops on the `h` or the `x`, and
                     * `ld a, (ix + 05h)` is how real sources write it. */
                    value dv = 0;
                    if (num_parse(ds, (int) (p - ds), &dv)) {
                        d = (int) dv;
                        got = true;
                    }
                }
                if (!got) {
                    /* A name or a sum, which is what a structure field looks
                     * like: `RES.LIL 4, (IX+sysvar_vpd_pflags)` is how BBC
                     * BASIC reaches MOS's system variables, and it is most of
                     * that program's use of an index register.
                     *
                     * The sign is applied to the whole of it and not to the
                     * first term -- `(ix-v+1)` with `v` five is -6 in the
                     * reference, not -4 -- which is what negating the result
                     * below already does.
                     *
                     * A name still ahead is refused. The reference has a
                     * second pass and resolves it; here the displacement is
                     * one byte of an instruction that is being written now,
                     * and there is nowhere to put a fixup for a field that is
                     * not a whole operand. Same position as the count of a DS
                     * and the value of an EQU. */
                    p = ds;
                    fwd_reset(NULL);
                    uint8_t dmask = 0;
                    /* Narrowed where it lands, like every value an operand
                     * carries: a displacement is one signed byte and the
                     * range test below is what enforces it. The evaluator is
                     * wider than the machine; nothing an operand holds is.
                     * See evalue. */
                    evalue dv32 = 0;
                    if (!expr_value(&dv32, &p, e, &dmask)) {
                        return false;
                    }
                    d = (int) dv32;
                    if (expr_fwd != NULL) {
                        state.err = ZAP_E_LABEL_DEFINED_ALREADY;

                        return false;
                    }
                }
                op->disp = neg ? -d : d;
                if (op->disp < -128 || op->disp > 127) {
                    /* "Index register offset exceeded" there. One signed byte
                     * is what the instruction has room for, so anything else
                     * would be emitted truncated and silently wrong. */
                    state.err = ZAP_E_INDEX_OFFSET_OUT_RANGE;

                    return false;
                }
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
            }

            if ((op->mode & INDIRECT) != 0) {
                if (*p != ')') {
                    state.err = ZAP_E_EXPECTED;

                    return false;
                }
                p++;
                /* Skips the spaces after the closing parenthesis, which the
                 * general path below already does after its own `)`.
                 *
                 * Needed in one case: finding the comma before a third
                 * operand, as in `res 5, (ix+1) , h`, which the reference
                 * accepts and assembles the same as the unspaced form. Doing
                 * it here rather than in assemble_line confines the cost to
                 * indirect register operands. */
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
            }
            *pp = p;

            return true;
        }

        /* Not a register -- but it can still be a literal.
         *
         * A hexadecimal constant written with a trailing h begins with one of
         * a..f, so `ld hl, aabbcch` arrives here looking exactly like a name.
         * Rewinding to the start of the token is all it takes: num_ch admits
         * letters, so the literal scan below reads the whole thing, and the
         * closing parenthesis of an indirect operand is handled there too.
         *
         * The token is not scanned again to find that out. C_NUM contains
         * every character C_NAME does and three more -- $, # and % -- so the
         * literal scan re-reads exactly the characters just read and stops in
         * the same place, unless the character that ended the name run is one
         * of those three. One class test answers that, where a second pass
         * would walk the whole token, and the token here is usually a label:
         * the longest thing an operand can be.
         *
         * The apostrophe needs no special case, being outside C_NUM as
         * well. */
        if (!num_ch(*nend)) {
            known_end = nend;
        }
        p = s;
    }

    PTRUNC_AT(2);

    /* A literal, or an expression. */
    {
        const char* s = p;
        if (known_end != NULL) {
            /* Already scanned, by the register path that rewound to here. */
            p = known_end;
        } else {
            if (*p == '-' || *p == '+') {
                p++;
            }
            /* Bounded, like every character scan here; see the header of this
             * file. */
            while (p < e && num_ch(*p)) {
                p++;
            }
        }
        const int n = (int) (p - s);
        bool neg = false;
        const char* ns = s;
        int nn = n;
        if (n > 0 && (*s == '-' || *s == '+')) {
            neg = *s == '-';
            ns = s + 1;
            nn = n - 1;
        }
        /* 0x... and plain decimal, which is what an instruction stream is
         * made of, without the general parser. num_parse has to consider a
         * leading $ or # or %, a trailing h or b or o, and a lone character
         * being decimal so that a..f are not hex digits -- none of which can
         * apply to a run that starts with a digit and holds only digits. */
        int total = 0;
        if (nn == 0) {
            /* Nothing the ordinary scan could take, because the operand begins
             * with a character that is not a C_NUM one: a character literal, a
             * bracketed group, or a unary `~`. The evaluator knows all three,
             * and knows what follows them, so it takes the whole operand.
             *
             * Also where a negated label lands, from the two places below: the
             * fast path reads `-name` as a sign and a name and has nowhere to
             * put the sign, but `-f1 + f2` is representable and the evaluator
             * is what knows that. Reached by goto rather than by testing for
             * it, so the ordinary operand pays nothing for the detour. */
full_expression:
            p = s;
            fwd_reset(NULL);
            uint8_t fwdmask = 0;
            evalue wide = 0;
            if (!expr_value(&wide, &p, e, &fwdmask)) {
                return false;
            }
            total = (int) wide;
            if (!fwd_finish(op)) {
                /* Every way fwd_finish can refuse is a shape the reference
                 * assembles -- a negated label, a complemented one, a third
                 * symbol, any operator but plus and minus -- so all of them
                 * are kept as text rather than refused. */
                if (!defer_expr(s, (int) (p - s), op)) {
                    return false;
                }
                total = 0;
            }
            goto have_value;
        }

        int v = 0;
        bool got = false;
        if (nn == 1 && ns[0] == '$') {
            /* The address of the instruction being assembled. `$` alone; with
             * hex digits after it the scan has already taken them and it is
             * the radix prefix instead. */
            v = state.org + (int) (state.o - state.out);
            got = true;
        } else if (ns[0] == '@') {
            /* `@f` and `@n` are the next anonymous label, `@b` and `@p` the
             * previous one. Reserved spellings, whatever a local of that name
             * would mean -- and a local really can be called `@b`: the
             * reference accepts the definition and then leaves it unreachable,
             * because the reference wins here. Only these exact two-character
             * spellings; `@bb` and `@ff` are ordinary locals. */
            const char k2 = nn == 2 ? (char) (ns[1] | 0x20) : 0;
            if (k2 == 'b' || k2 == 'p') {
                /* Backward is not a reference at all: the address is already
                 * known, so this is the same as a label defined above. */
                if (!state.anon_has_prev) {
                    state.err = ZAP_E_NO_ANONYMOUS_LABEL_ABOVE_ONE;

                    return false;
                }
                v = state.anon_prev;
            } else {
                const sym* sp;
                if (k2 == 'f' || k2 == 'n') {
                    sp = anon_next();
                } else {
#ifdef DUP_LOCINTERN
                    if (loc_intern(ns, nn) == NULL) {
                        return false;
                    }
#endif
                    /* A local label, and nothing else: no radix accepts a
                     * leading at sign, and the reference does not test a local
                     * against the number formats either. Going straight to the
                     * lookup also keeps `@abch` from being read as hexadecimal
                     * by the trailing-h rule below. */
                    sp = loc_intern(ns, nn);
                }
                if (sp == NULL) {
                    return false;
                }
                if (sp->defined) {
                    v = sp->addr;
                } else if (neg) {
                    goto full_expression;
                } else {
                    op->fwd = sp;
                    v = 0;
                }
            }
            got = true;
        } else if (nn >= 3 && ns[0] == '0' && (ns[1] | 0x20) == 'x') {
            got = hex_digits(ns + 2, nn - 2, &v);
        } else if (nn >= 2 && (ns[nn - 1] | 0x20) == 'h') {
            /* A trailing h, which is the form the reference's own corpus
             * writes: `aabbcch`, and `0ffh`. It begins with a letter as often
             * as not, so it arrives here only because the register path
             * rewinds to it. */
            got = hex_digits(ns, nn - 1, &v);
        } else if (nn > 0 && digit_ch(ns[0])) {
            /* First digit outside the loop, for the reason given at the
             * displacement above: a one-digit literal then needs no multiply,
             * and `im 2`, `rst 0`, `bit 3` and the rest of the small decimals
             * are exactly that. */
            /* Unsigned for the same reason same_ci is: two signed ints
             * compared with `<` cost a `call pe, __setflag` to repair the
             * flags on overflow, inside a loop that runs once per digit. */
            int acc = ns[0] - '0';
            unsigned k = 1;
            for (; k < (unsigned) nn; k++) {
                if (!digit_ch(ns[k])) {
                    break;
                }
                acc = acc * 10 + (ns[k] - '0');
            }
            if (k == (unsigned) nn) {
                v = acc;
                got = true;
            }
        }
#ifdef DUP_NUMTOK
        if (!got && nn > 0) {
            dup_hash_sink = numeric_token(ns, nn);
        }
#endif
        if (!got && nn > 0 && !numeric_token(ns, nn)) {
            /* Not a literal in any radix, so it is a label.
             *
             * Tried after the literal forms rather than before them, because a
             * hexadecimal constant with a trailing h begins with a letter too:
             * `aabbcch` is a number and `aabbcc` is a name, and only the
             * suffix tells them apart.
             *
             * The decision is whether any radix accepts the token, not what it
             * starts with. `$42`, `#42` and `%1010` are literals that begin
             * with neither a letter nor a digit, while `2b`, `1z` and `123abc`
             * are labels that begin with a digit -- and the reference lets
             * those be referenced as well as defined.
             *
             * A label already defined is its address. One that is not is
             * carried on the operand for the emitter to record, because where
             * the bytes land is not known until the row is chosen. */
#ifdef DUP_INTERN
            if (sym_intern(ns, nn) == NULL) {
                return false;
            }
#endif
            const sym* sp = sym_intern(ns, nn);
            if (sp == NULL) {
                return false;
            }
            if (sp->defined) {
                v = sp->addr;
                got = true;
            } else if (neg) {
                /* `-label` with the address not known yet. On its own it
                 * cannot be represented -- a fixup adds its first symbol --
                 * but `-start + end` can, so the evaluator decides rather
                 * than this path, which has nowhere to keep the sign. */
                goto full_expression;
            } else {
                op->fwd = sp;
                v = 0;
                got = true;
            }
        }
        if (!got) {
            value gv = 0;
            if (nn <= 0 || !num_parse(ns, nn, &gv)) {
                state.err = ZAP_E_EXPECTED_VALUE;

                return false;
            }
            v = (int) gv;
        }
        total = neg ? -v : v;

        /* An expression, if an operator follows the term just read. One table
         * lookup on the character that ended it, which is what the other
         * twenty-eight operands in twenty-nine pay for this feature.
         *
         * A forward reference continues into one by being kept as text: the
         * fixup carries two symbols and a constant, and `TENDIF*256+TTHEN` is
         * not that shape. See defer_expr. */
        {
            const char* q = p;
            while (q < e && is_space_ch(*q)) {
                q++;
            }
            if (exop[(uint8_t) *q] != 0) {
                /* The term already read may itself be a forward reference --
                 * `later + 4` reaches here with `later` in op->fwd -- so the
                 * evaluator is seeded with it rather than refusing. */
                fwd_reset(op->fwd);
                op->fwd = NULL;
                p = q;
                uint8_t fwdmask = fwd_live();
                evalue wide = total;
                if (!expr_climb(&wide, &p, e, 1, 0, &fwdmask)) {
                    return false;
                }
                total = (int) wide;
                if (!fwd_finish(op)) {
                    /* Same as the branch above: an expression a fixup cannot
                     * hold is kept as text rather than refused. This is the
                     * site that matters in practice, because an operand that
                     * begins with a name reaches the expression through here
                     * and not through `full_expression`. */
                    if (!defer_expr(s, (int) (p - s), op)) {
                        return false;
                    }
                    total = 0;
                }
            }
        }

have_value:
        op->imm = total;
        op->mode |= IMM;

        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if ((op->mode & INDIRECT) != 0) {
            if (*p != ')') {
                state.err = ZAP_E_EXPECTED;

                return false;
            }
            p++;

            /* The positional rule, which is what makes `(` mean two things.
             *
             * A parenthesis that opens the operand and whose match closes it
             * is indirection: `ld a, (var)`. One whose match does *not* close
             * it was grouping all along -- `add a, (RTABLE-DTABLE)/2` -- and
             * up to the `/` the two are indistinguishable.
             *
             * So an operand is parsed as indirection first and reinterpreted
             * here, rather than scanned ahead to its matching parenthesis
             * before its shape is known. Scanning ahead would cost every
             * `(hl)` and `(ix+d)` in the file; this costs one table lookup on
             * the operands that reach the general parse with a parenthesis in
             * front, and the register path has already taken the common ones
             * out.
             *
             * The rewind re-reads the operand from the start: `*pp` still
             * holds where it began, because nothing writes it until the
             * end. */
            while (p < e && is_space_ch(*p)) {
                p++;
            }
            if (exop[(uint8_t) *p] != 0) {
                op->mode &= (uint8_t) ~INDIRECT;
                op->fwd = NULL;
                op->fwd2 = NULL;
                s = *pp;

                goto full_expression;
            }
        }
        *pp = p;
    }

    return true;
}

static inline const char* lit_value(const char* p, const char* e, evalue* out) {
    const char* q = p;

    /* A sign, which the operand parser has always taken and these did not.
     * `DB 1, 2, 3, -1` sends one item in four back to the evaluator without
     * it, and a table of signed bytes is what DB is for. */
    bool neg = false;
    if (*q == '-' || *q == '+') {
        neg = *q == '-';
        q++;
    }
    if (!digit_ch(*q)) {
        return NULL;
    }

    const char* const d = q;
    while (q < e && num_ch(*q)) {
        q++;
    }
    const int nn = (int) (q - d);

    /* What ended the run has to end the item too. */
    const char* r = q;
    while (r < e && is_space_ch(*r)) {
        r++;
    }
    if (*r != ',' && *r != '\n' && *r != ';' && r < e) {
        return NULL;
    }

    evalue value = 0;
    int hv = 0;
    bool got;
    if (nn >= 3 && d[0] == '0' && (d[1] | 0x20) == 'x') {
        got = hex_digits(d + 2, nn - 2, &hv);
        value = hv;
    } else if (nn >= 2 && (d[nn - 1] | 0x20) == 'h') {
        got = hex_digits(d, nn - 1, &hv);
        value = hv;
    } else {
        /* First digit outside the loop, as the operand parser does it: a
         * one-digit value then needs no multiply, and `d * 10` is a call to
         * __imulu here.
         *
         * Six digits and no more, which is the same rule hex_digits keeps and
         * for the same reason: 999,999 is inside the machine's word, so `acc`
         * is an `int` and `acc * 10` is __imulu rather than the wider helper.
         * A longer run is declined and num_parse reads it. Seven digits is
         * 9,999,999 and the word holds 8,388,607, so seven is already too
         * many. */
        if (nn > 6) {
            return NULL;
        }
        int acc = d[0] - '0';
        unsigned k = 1;
        for (; k < (unsigned) nn; k++) {
            if (!digit_ch(d[k])) {
                break;
            }
            acc = acc * 10 + (d[k] - '0');
        }
        got = k == (unsigned) nn;
        value = acc;
    }
    if (!got) {
        return NULL;
    }

    *out = neg ? -value : value;

    return q;
}

/* The four ungrouped mnemonics: call, jp, jr and ret.
 *
 * Ten rows between them, each carrying its own mode test, because a row that
 * takes a condition code has to be reached whatever mode the operands were
 * parsed as. Kept out of line so that the register test appears once in the
 * hot path rather than twice. */
__attribute__((always_inline)) static inline const isa_row* match_row_cc(
    const insninfo* insn, const dop* a, const dop* b, uint8_t want) {
    const uint8_t has_cc = (uint8_t) (a->cc != 0);
    const uint8_t a0 = a->r0, a1 = a->r1, a2 = a->r2;
    const uint8_t b0 = b->r0, b1 = b->r1, b2 = b->r2;
    const uint8_t anone = a->noreg;
    const uint8_t bnone = b->noreg;
    const rowinfo* ri = insn->rows;

    for (uint8_t n = insn->count; n != 0; n--, ri++) {
        const uint8_t ccok = ri->ccok;
        if (ri->modes != want && !(ccok & has_cc)) {
            continue;
        }
        if ((uint8_t) ((ri->a0 & a0) | (ri->a1 & a1) | (ri->a2 & a2)
                       | (ri->aempty & anone) | ccok) != 0
            && (uint8_t) ((ri->b0 & b0) | (ri->b1 & b1) | (ri->b2 & b2)
                          | (ri->bempty & bnone)) != 0) {
            const isa_row* row = ri->row;
            if ((row->cpu & cpu_mask) == 0) {
                return NULL;
            }

            return row;
        }
    }

    return NULL;
}

/* always_inline, and it has to be: this has two callers, the ordinary path and
 * the suffixed one, and a second caller is enough for the compiler to stop
 * inlining it into the first -- which is the path every instruction takes. */
__attribute__((always_inline)) static inline const isa_row* match_row(const insninfo* insn,
                                                         const dop* a,
                                                         const dop* b) {
    const uint8_t want = (uint8_t) (shl4[a->mode & 15] | (b->mode & 15));

    /* Find the group, then scan it. Two loops rather than one, because the
     * two questions have nothing in common: which shape of operands the row
     * wants, and which registers.
     *
     * Asked as one loop, rejecting a group cost a whole turn of it -- the
     * counter test, moving the row pointer into iy, loading the mode and the
     * ccok flag, then the skip count and the next pointer, twenty instructions
     * to step over rows that could not match. The group table is five bytes a
     * row and rejecting one is a compare and a step. */
    uint8_t n = insn->ngroups;
    if (n == 0) {
        return match_row_cc(insn, a, b, want);
    }

    const grpinfo* g = insn->groups;
    while (g->modes != want) {
        if (--n == 0) {
            return NULL;
        }
        g++;
    }

    /* Already split, by whoever recognised the register. */
    const uint8_t a0 = a->r0, a1 = a->r1, a2 = a->r2;
    const uint8_t b0 = b->r0, b1 = b->r1, b2 = b->r2;
    const uint8_t anone = a->noreg;
    const uint8_t bnone = b->noreg;

    /* Operand A on its own, and B only if A survives.
     *
     * The question is whether the operand shares a bit with what the row
     * accepts, so the bits are tested where they are rather than turned into
     * two 0/1 values and ANDed -- each `(g != 0)` would be a compare and a
     * branch to pick between two constants, and both sides would be computed
     * before either was looked at.
     *
     * This is the line to spend care on: several rows reach the register test
     * for every instruction, and most rejections are rows of the right shape
     * with the wrong registers. Rejecting on A alone skips B entirely for the
     * majority of them.
     *
     * No mode test here and no ccok test: being in the group is the answer,
     * and a mnemonic with a ccok row anywhere in it has no groups at all. */
    const rowinfo* ri = g->rows;
    for (uint8_t k = g->count; k != 0; k--, ri++) {
        if ((uint8_t) ((ri->a0 & a0) | (ri->a1 & a1) | (ri->a2 & a2)
                       | (ri->aempty & anone)) != 0
            && (uint8_t) ((ri->b0 & b0) | (ri->b1 & b1) | (ri->b2 & b2)
                          | (ri->bempty & bnone)) != 0) {
            const isa_row* row = ri->row;
            if ((row->cpu & cpu_mask) == 0) {
                return NULL;
            }

            return row;
        }
    }

    return NULL;
}

__attribute__((always_inline)) static inline uint8_t ddfd_prefix(const dop* op) {
    if ((op->r1 & RP1_IX) != 0) {
        return 0xDD;
    }
    if (((op->r1 & RP1_IY) | (op->r2 & RP2_IY)) != 0) {
        return 0xFD;
    }

    return 0;
}

__attribute__((always_inline)) static inline uint8_t transform(emitted* out, dop* op, uint8_t type) {
    switch (type) {
        case TR_IR0:
            if (((op->r1 & RP1_XYL) | (op->r2 & RP2_XYL)) != 0) {
                out->opcode |= 0x01;
            }
            break;
        case TR_IR3:
            if (((op->r1 & RP1_XYL) | (op->r2 & RP2_XYL)) != 0) {
                out->opcode |= 0x08;
            }
            break;
        case TR_Z:
            out->opcode |= op->reg_index;
            break;
        case TR_Y:
            if ((op->mode & IMM) != 0) {
                /* The bit number of `bit n, (hl)` and `bit n, (ix+d)`. The
                 * register forms use TR_BIT and are checked there; every other
                 * TR_Y is a register in this slot and never takes this arm. */
                if (op->fwd != NULL) {
                    op->mode &= (uint8_t) ~IMM;

                    return TRF_DEFER;
                }
                if (op->imm > 7) {
                    state.err = ZAP_E_INVALID_BIT_NUMBER;

                    return TRF_ERR;
                }
                /* Shifted rather than looked up in a table, and not masked,
                 * because the reference does not mask: `bit -1, a` is CB FF
                 * there -- the whole shifted value ORed into the opcode. A
                 * table indexed by `v & 7` would give CB 7F. */
                out->opcode |= (uint8_t) ((unsigned) op->imm << 3);
            } else {
                out->opcode |= shl3[op->reg_index & 7];
            }
            break;
        case TR_P:
            out->opcode |= shl4[op->reg_index & 15];
            break;
        case TR_CC:
            out->opcode |= shl3[op->cc_index & 7];
            break;
        case TR_N:
            /* RST, and the only row with this transform. The eight addresses
             * are the multiples of eight below 0x40, which is every value that
             * ORs into 0xC7 without touching a bit that is already there. */
            op->mode &= (uint8_t) ~IMM;
            if (op->fwd != NULL) {
                return TRF_DEFER;
            }
            if (((unsigned) op->imm & ~0x38u) != 0) {
                state.err = ZAP_E_RESTART_ADDRESS;

                return TRF_ERR;
            }
            out->opcode |= (uint8_t) op->imm;
            break;
        case TR_BIT:
            op->mode &= (uint8_t) ~IMM;
            if (op->fwd != NULL) {
                return TRF_DEFER;
            }
            if (op->imm > 7) {
                state.err = ZAP_E_INVALID_BIT_NUMBER;

                return TRF_ERR;
            }
            out->opcode |= (uint8_t) ((unsigned) op->imm << 3);
            break;
        case TR_SELECT: {
            /* IM, and the only row with this transform. Modes 0, 1 and 2 are
             * y = 0, 2 and 3. Above 2 the reference refuses; below 0 it does
             * not, and assembles `im 0`, so neither does this. */
            op->mode &= (uint8_t) ~IMM;
            if (op->fwd != NULL) {
                return TRF_DEFER;
            }
            uint8_t y = 0;
            if (op->imm == 1) {
                y = 2;
            } else if (op->imm == 2) {
                y = 3;
            } else if (op->imm > 2) {
                state.err = ZAP_E_INTERRUPT_MODE;

                return TRF_ERR;
            }
            out->opcode |= shl3[y & 7];
            break;
        }
        default:
            break;
    }

    return TRF_OK;
}

__attribute__((always_inline)) static inline bool emit_row(const isa_row* row, dop* a, dop* b, uint8_t suffix) {
    if (!out_reserve()) {
        return false;
    }

    /* One cursor for the whole instruction rather than state.out[state.pos++] per
     * byte. put() reloaded both the output base and the position, added them,
     * stored the byte and stored the position back, for every byte written --
     * twenty-three loads of those two fields in this function alone. The
     * reservation above is what makes a bare cursor safe: room for the
     * longest form is already there, so nothing between here and the
     * write-back can move the buffer. */
    ETRUNC_AT(1);

    uint8_t* o = state.o;

    /* The suffix byte goes in front of everything, including the DD or FD an
     * index register brings: `ld.lil ix, nn` is 5B DD 21 ...
     *
     * Whether the row takes this suffix is asked here rather than while the
     * row is chosen, because the row is chosen by the shape of the operands
     * and this is not one of them. `ld.lil a, b` matches the register-to-
     * register row perfectly well and is still refused -- "Suffix not matching
     * mnemonic / ADL mode" in the reference -- since a suffix only means
     * something where the instruction touches memory, the stack or an
     * address. Which rows those are is in the table already; the generator
     * transcribed the field with the rest of the row. */
    /* Nothing here survives into the ordinary path: emit_row is always
     * inlined and its one hot caller passes a literal zero, so the whole of
     * this folds away there. The suffixed copy lives in uncommon_line. */
    if (suffix != 0) {
        if ((row->flags & suffix) == 0) {
            state.err = ZAP_E_INSTRUCTION_NO_MODE_SUFFIX;

            return false;
        }
        *o++ = suffix_code(suffix);
    }

    emitted out;
    out.prefix1 = 0;
    out.prefix2 = row->prefix;
    out.opcode = row->opcode;

    if (row->flags & F_DDFDOK) {
        const uint8_t p1 = ddfd_prefix(a);
        const uint8_t p2 = ddfd_prefix(b);
        out.prefix1 = ((p1 == 0 && p2 != 0) || ((a->mode & INDIRECT) == 0 && p1 != 0 && p2 != 0))
                      ? p2 : p1;
    }

    /* Tested rather than called. transform is a real function with a switch
     * in it, and TR_NONE is the commonest case by a wide margin -- every
     * instruction whose operands do not fold into the opcode. A load and a
     * compare replaces a call, a dispatch and a return. */
    if (row->transformA != TR_NONE) {
        const uint8_t r = transform(&out, a, row->transformA);
        if (r != TRF_OK
            && (r == TRF_ERR
                || !fold_defer(row->transformA, a, out.prefix1, out.prefix2,
                               row->flags, (int) (o - state.out)))) {
            return false;
        }
    }
    if (row->transformB != TR_NONE) {
        const uint8_t r = transform(&out, b, row->transformB);
        if (r != TRF_OK
            && (r == TRF_ERR
                || !fold_defer(row->transformB, b, out.prefix1, out.prefix2,
                               row->flags, (int) (o - state.out)))) {
            return false;
        }
    }

    ETRUNC_AT(2);

    /* The ordinary shape first: no index prefix and no displacement, which is
     * every instruction that is not an `(ix+d)` form.
     *
     * The chain below is six tests to place at most four bytes, and one of
     * them -- whether the opcode goes after the displacement rather than
     * before it -- is only ever true for `bit n, (ix+d)` and its relatives:
     * DD or FD, then CB, then a displacement. Computing that on every
     * instruction, and testing it twice, is what this skips. `prefix1` is zero
     * whenever there is no index register, so one load answers it. */
    const uint8_t dflags = (uint8_t) (row->flags & (F_DISPA | F_DISPB));
    if (out.prefix1 == 0 && dflags == 0) {
        if (out.prefix2 != 0) {
            *o++ = out.prefix2;
        }
        *o++ = out.opcode;
    } else {
        const bool dd_before_opcode =
            (out.prefix1 == 0xDD || out.prefix1 == 0xFD) && out.prefix2 == 0xCB
            && dflags != 0;

        if (out.prefix1 != 0) {
            *o++ = out.prefix1;
        }
        if (out.prefix2 != 0) {
            *o++ = out.prefix2;
        }
        if (!dd_before_opcode) {
            *o++ = out.opcode;
        }
        if (dflags & F_DISPA) {
            *o++ = (uint8_t) (a->disp & 0xFF);
        }
        if (dflags & F_DISPB) {
            *o++ = (uint8_t) (b->disp & 0xFF);
        }
        if (dd_before_opcode) {
            *o++ = out.opcode;
        }
    }

    ETRUNC_AT(3);

    /* A relative displacement is measured from the instruction after this one,
     * so it is the last thing written and needs no width decision. */
    if (row->transformA == TR_REL || row->transformB == TR_REL) {
        const dop* rel = (row->transformA == TR_REL) ? a : b;
        if (rel->fwd != NULL) {
            /* Forward: the displacement is not known, so a zero goes down and
             * the fixup carries the address it will be measured from. Whether
             * it is in reach is decided when it is patched. */
            if (!fix_add(rel->fwd, rel->fwd2, rel->imm,
                         rel->fwd2_neg ? FIX_SUB2 : 0,
                         (int) (o - state.out))) {
                return false;
            }
            *o++ = 0;
        } else {
            const int d = rel->imm - (state.org + (int) (o - state.out) + 1);
            if (d < -128 || d > 127) {
                state.err = ZAP_E_RELATIVE_JUMP_TOO_FAR;

                return false;
            }
            *o++ = (uint8_t) d;
        }
    } else {
        /* How wide an address is: the suffix says, if there is one, and the
         * ADL mode says otherwise. `ld.lis hl, 0x1234` is two bytes of
         * immediate in ADL mode and `ld.sil hl, 0x123456` is three out of it,
         * so this cannot read the mode alone.
         *
         * Asked inside each branch rather than once above them. Hoisted, it is
         * a live value across both and an instruction with no immediate --
         * which is most of them -- computes it for nothing. */
#define SFX_WIDE (suffix != 0 ? (suffix & (S_SIS | S_LIS)) == 0 : state.adl)
        if ((a->mode & IMM) != 0 && (row->condA & (IMM_N | IMM_MMN))) {
            if (a->fwd != NULL
                && !fix_add(a->fwd, a->fwd2, a->imm,
                            (uint8_t) (((row->condA & IMM_N) ? 1
                                                             : (SFX_WIDE ? 3 : 2))
                                       | (a->fwd2_neg ? FIX_SUB2 : 0)),
                            (int) (o - state.out))) {
                return false;
            }
            warn_imm(a->imm, (row->condA & IMM_N) ? 1 : (SFX_WIDE ? 3 : 2));
            o = emit_imm(o, a, row->condA, SFX_WIDE);
        }
        if ((b->mode & IMM) != 0 && (row->condB & (IMM_N | IMM_MMN))) {
            if (b->fwd != NULL
                && !fix_add(b->fwd, b->fwd2, b->imm,
                            (uint8_t) (((row->condB & IMM_N) ? 1
                                                             : (SFX_WIDE ? 3 : 2))
                                       | (b->fwd2_neg ? FIX_SUB2 : 0)),
                            (int) (o - state.out))) {
                return false;
            }
            warn_imm(b->imm, (row->condB & IMM_N) ? 1 : (SFX_WIDE ? 3 : 2));
            o = emit_imm(o, b, row->condB, SFX_WIDE);
        }
#undef SFX_WIDE
    }

    state.o = o;

    return true;
}

#endif /* ZAP_H */
