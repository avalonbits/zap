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
 * It exists to agree with `ez80asm` byte for byte and to be fast enough that
 * assembling on the machine itself is not something to avoid. Both halves of
 * that are load-bearing: where the reference does something surprising -- no
 * operator precedence, `IF a == b` evaluating `a` and discarding the rest,
 * `0bh` reading as hex before binary -- this reproduces it under `-ez80`
 * rather than being right and incompatible. What the two disagree about on
 * purpose is listed at the option below.
 *
 * ONE PASS. A reference to a label further down cannot be resolved where it
 * is read, so the output is held in memory and patched at the end. That is
 * the whole design: everything else follows from never looking at a line
 * twice.
 *
 * ONE FILE, and not for the usual reasons. The eZ80 has no cache and a
 * function call is expensive, so what matters is which of the hot path the
 * compiler can see at once -- `assemble_line` has the operand parser, the row
 * match and the emitter inlined into it, and the measurements that say why are
 * in .internal/performance-notes.md. Splitting it would be tidier and slower,
 * and the second part has been measured more than once.
 *
 * EVERY SCAN IS BOUNDED. A loop that walks a character pointer compares that
 * pointer against the end of the buffer as well as testing what it points at,
 * and that is not defensive programming -- unbounded, these loops have come
 * out of the compiler rotated the wrong way: the pointer pre-decremented and
 * each turn testing one character past it, so the first is never examined and
 * the scan stops one short. It has happened five times on five different
 * loops, it reduces to nothing, and **the host build is correct every time**,
 * so nothing here can catch it except reading the source or the assembly. The
 * last occasion rotated four loops in a function whose source was unchanged,
 * because a function was added between two others and the allocation moved.
 * Two conditions are what stops it. `test/run.sh` checks that the source
 * keeps them, and the price is in .internal/performance-notes.md.
 *
 * WHERE THIS CAME FROM. It began as `dzap`, a stripped assembler written to
 * find a floor: the earlier zap spent about 830 cycles per source byte and it
 * was not clear how much of that was assembling and how much was machinery.
 * Building the same instruction stream with the machinery gone answered it,
 * and then every feature -- labels, expressions, directives, EQU, INCLUDE,
 * ASSUME, conditionals, macros, mode suffixes -- was added back one at a time
 * and priced on the Agon before the next one started. It arrived at the same
 * feature set for a third of the cycles, so it replaced what it was measuring.
 * The prices are all in the notes, and so are the things that were tried and
 * put back.
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
 * Four bytes, and the eZ80's is three, so this is deliberately wider than the
 * machine -- which is the opposite of what the rest of this file does. Every
 * other quantity here is the machine's word on purpose: an address, a count,
 * an immediate and a displacement all fit in 24 bits and paying for 32 would
 * be paying on every line.
 *
 * An expression is the one place they do not. `DW32` and `BLKL` are four bytes
 * wide, the reference fills them with `0x55555555`, `~0` and `-2147483648`,
 * and no amount of care at the emitter recovers a bit the evaluator has
 * already dropped. The reference evaluates in 32 bits, so this is also the
 * width that agrees with it wherever the two would otherwise differ.
 *
 * It costs **0.10s of isa_real's 5.74**, 1.8%, and it cost 0.42 before three
 * rounds of taking it off the paths that do not need it: the fast literal
 * readers decline anything wider than the machine and let num_parse have it,
 * and the emitters narrow once when the width is three or less. See
 * .internal/performance-notes.md, which has the decomposition and the two
 * things that measured and were not kept.
 *
 * The evaluator is not on the path most operands take: a register, a plain
 * literal and a bare name each have a reader of their own that never enters
 * it, and keeping those readers narrow is most of why the price is 1.8% and
 * not 7.4%.
 *
 * Narrowing this is a one-line edit and would look like free speed, which is
 * why the assertion is here rather than in a comment. It fires on the target
 * and not on the host, where `int` is already four bytes -- and that asymmetry
 * is the whole reason the width was a question at all. */
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
    /* The register set, split into byte planes and kept that way.
     *
     * It is a bitmask whose highest bit is R_I at 2^20, and every use of it is
     * a mask or a test against zero -- never arithmetic. Held as one 24-bit
     * word each of those is a call, because AND is an 8-bit instruction here;
     * split, they are the byte operations the chip has.
     *
     * Split at the point the register is recognised rather than where it is
     * used. match_row used to do it for both operands on every instruction,
     * which cost two calls to __ishru even for `nop`, an instruction with no
     * register operands at all. */
    uint8_t r0, r1, r2;

    /* Whether the three above are all zero, decided where they are set rather
     * than re-derived. match_row wanted it for both operands on every
     * instruction, which is six loads and four ORs to learn something the
     * operand has known since it was built. */
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
     * it is 61% of the operands that name more than one label ahead.
     *
     * Two is the limit, and not an arbitrary one: the fixup that carries them
     * is sixteen bytes because that made its indexing a shift instead of a
     * multiply, and two symbols is what fits. */
    const sym* fwd2;
    bool fwd2_neg;

    /* An instruction's immediate is at most three bytes -- a 24-bit address in
     * ADL mode -- so it is held in the machine's own word rather than the
     * 32-bit `value` the expression evaluator deals in. Same reasoning as the
     * register mask above, and the same invisibility on a host where int is 32
     * bits anyway.
     *
     * Truncating from `value` on the way in is what the emitter would do
     * regardless: it writes the low one, two or three bytes and the rest was
     * never going to be looked at. */
    int imm;
} dop;

#ifdef AGONDEV
/* Pinned, because the way a struct like this grows is one innocent `bool` at a
 * time, and every byte of it is copied twice a line from the empty template.
 *
 * Twenty-three until two of those bools turned out to be bits that were
 * already here; see `mode`. Twenty-one is not a power of two and does not need
 * to be -- nothing indexes an array of these. */
_Static_assert(sizeof(dop) == 21, "an operand is twenty-one bytes");
#endif

/* Room for the longest instruction, asked for once.
 *
 * Testing on every byte written meant a bounds check per output byte when an
 * instruction knows in advance that it cannot need more than a handful. The
 * longest form here is two prefixes, an opcode, two displacements and two
 * three-byte immediates. */
/* Twelve was the longest instruction; a mode suffix puts one more byte in
 * front of it. */
/* The longest label the reference takes, counting the `@` of a local. */
#define LABEL_MAX 64

#define OUT_MAX_INSN 13

/* How much of a failing line is echoed back. Longer than any line anybody
 * writes, and a line longer than this is truncated rather than refused --
 * the report is a courtesy and must never itself be a failure. */
#define ERRLINE_MAX 128

/* The longest file name INCLUDE and INCBIN will take. Fixed, because the name
 * is copied into a frame that has to outlive the line it came from, and into
 * `dz.errpath` when an include fails. */

/* ------------------------------------------------------------- symbols */

/* A label, and where it turned out to be.
 *
 * The name is copied. Source lines live in the reader's buffer and are gone
 * as soon as it refills, so a pointer into one is a pointer into the next
 * line by the time a forward reference is resolved.
 */
#define INCLUDE_NAME_MAX 80

/* How many parameters a macro may take. The reference's own corpus does not go
 * past four; sixteen is more than any of it and keeps the argument spans on the
 * stack rather than in another allocation. */
#define MACRO_MAXPARAM 8

typedef struct _macro macro;
/* Where a parameter appears in the body, found once when the body is read.
 *
 * The body does not change after ENDMACRO and neither do the places its
 * parameters occur, so deciding them at expansion time meant classifying every
 * token of every line and asking every parameter about every identifier --
 * **1,940 cycles a parameter**, on every invocation, to rediscover something
 * settled when the macro was written.
 *
 * Walked with a pointer and never subscripted, which is why the record is its
 * natural size rather than padded to a power of two. It is five bytes on the
 * Agon and six here, and `marks[i]` on either would be a call to __imulu
 * because the eZ80 multiply is 8-bit -- the expansion steps a cursor instead,
 * and the marks are in body order, so one pass covers every line. Padding it
 * to eight was tried first and does not work: `int` is three bytes there and
 * four here, so no single amount of padding is a power of two on both. */
typedef struct {
    int off;        /* where in the body the name starts */
    uint8_t k;      /* which parameter */
    uint8_t len;    /* how many bytes of body the argument replaces */
} macmark;

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

    /* Where the MACRO directive was, so a failure in the body can be reported
     * against the line of the *file* it was written on rather than against its
     * index in the body. The reference does the same: a body line is "line 2"
     * of the source, not "line 1" of the macro.
     *
     * The name is copied into the arena rather than pointed at. A reader's
     * file name lives exactly as long as the reader, and a header of macros
     * is defined in one file and used from another -- which is the normal
     * shape and the one a borrowed pointer would get wrong. */
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
 * It was a `const char*` for a reason that has gone: zap had to fit in a
 * moslet, and a pointer to a literal is the smallest thing that can carry a
 * message. It costs the same either way on the path that matters -- the store
 * happens only when something has already failed -- and a code is two
 * instructions where a string address is two, one byte where a pointer is
 * three, and the one thing a caller that is not `main` can act on.
 *
 * zap is meant to be usable as a library, and a library that reports by
 * handing back English is a library nobody can branch on.
 *
 * The text lives in one table rather than at 120 sites. Eleven of them said
 * "out of memory for labels" and whether the compiler merged those was its
 * business, not ours. */
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
    ZAP_E_INVALID_LABEL,
    ZAP_E_LABEL_DEFINED_TWICE,
    ZAP_E_LABEL_TOO_LONG,
    ZAP_E_LINE_TOO_LONG,
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

/* Indexed by the code, so a message and its name cannot drift apart. */
static const char* const zap_err_text[] = {
    [ZAP_OK] = "no error",
    [ZAP_E_ADL_0_OR_1] = "ADL is 0 or 1",
    [ZAP_E_FILLBYTE_COME_BEFORE_SPACE_FILLS] = "FILLBYTE must come before the space it fills",
    [ZAP_E_IF_LEFT_OPEN_AT_END_FILE] = "IF left open at the end of the file",
    [ZAP_E_RELOCATE_DOES_NOT_NEST] = "RELOCATE does not nest",
    [ZAP_E_MACRO_WAS_NEVER_CLOSED] = "a MACRO was never closed",
    [ZAP_E_LABEL_CANNOT_NEGATED] = "a label cannot be negated",
    [ZAP_E_LABEL_DEFINED_ALREADY] = "a label here must be defined already",
    [ZAP_E_MACRO_PARAMETER_NOT_NUMBER_OR_MNEM] = "a macro parameter may not be a number or a mnemonic",
    [ZAP_E_STRING_DB] = "a string needs DB",
    [ZAP_E_UNARY_OPERATOR_VALUE] = "a unary operator needs a value",
    [ZAP_E_ADDRESS_OUTSIDE_24_BIT_RANGE] = "address outside the 24-bit range",
    [ZAP_E_ALIGN_POSITIVE_NUMBER] = "align needs a positive number",
    [ZAP_E_ALIGN_POWER_TWO] = "align needs a power of two",
    [ZAP_E_IF_WAS_NEVER_CLOSED] = "an IF was never closed",
    [ZAP_E_BAD_ESCAPE_IN_STRING] = "bad escape in string",
    [ZAP_E_BLK_POSITIVE_NUMBER] = "blk needs a positive number",
    [ZAP_E_CANNOT_OPEN_SOURCE] = "cannot open source",
    [ZAP_E_CANNOT_OPEN_FILE] = "cannot open the file",
    [ZAP_E_CANNOT_READ_FILE] = "cannot read the file",
    [ZAP_E_CANNOT_REOPEN_FILE] = "cannot reopen the file",
    [ZAP_E_CANNOT_SET_FILE_ASIDE] = "cannot set the file aside",
    [ZAP_E_CONDITIONALS_DO_NOT_NEST] = "conditionals do not nest",
    [ZAP_E_DIVISION_BY_ZERO] = "division by zero",
    [ZAP_E_DS_POSITIVE_NUMBER] = "ds needs a positive number",
    [ZAP_E_EXPECTED] = "expected )",
    [ZAP_E_EXPECTED_OR] = "expected << or >>",
    [ZAP_E_EXPECTED_AFTER_ADL] = "expected = after ADL",
    [ZAP_E_EXPECTED_ADL] = "expected ADL",
    [ZAP_E_EXPECTEDX] = "expected ]",
    [ZAP_E_EXPECTED_CHARACTER] = "expected a character",
    [ZAP_E_EXPECTED_FILE_NAME] = "expected a file name",
    [ZAP_E_EXPECTED_MACRO_NAME] = "expected a macro name",
    [ZAP_E_EXPECTED_QUOTED_FILE_NAME] = "expected a quoted file name",
    [ZAP_E_EXPECTED_VALUE] = "expected a value",
    [ZAP_E_EXPECTED_INSTRUCTION] = "expected an instruction",
    [ZAP_E_EXPRESSION_NESTED_TOO_DEEPLY] = "expression nested too deeply",
    [ZAP_E_FILE_NAME_TOO_LONG] = "file name too long",
    [ZAP_E_INCLUDES_NESTED_TOO_DEEPLY] = "includes nested too deeply",
    [ZAP_E_INDEX_OFFSET_OUT_RANGE] = "index offset out of range",
    [ZAP_E_INVALID_LABEL] = "invalid label",
    [ZAP_E_LABEL_DEFINED_TWICE] = "label defined twice",
    [ZAP_E_LABEL_TOO_LONG] = "label too long",
    [ZAP_E_LINE_TOO_LONG] = "line too long",
    [ZAP_E_MACRO_PARAMETER_NAME_TOO_LONG] = "macro parameter name too long",
    [ZAP_E_MACROS_DO_NOT_NEST] = "macros do not nest",
    [ZAP_E_MACROS_NESTED_TOO_DEEPLY] = "macros nested too deeply",
    [ZAP_E_NO_ADL_MODE_CPU] = "no ADL mode on this CPU",
    [ZAP_E_NO_IF_OPEN] = "no IF is open",
    [ZAP_E_NO_MACRO_OPEN] = "no MACRO is open",
    [ZAP_E_NO_RELOCATE_OPEN] = "no RELOCATE is open",
    [ZAP_E_NO_ANONYMOUS_LABEL_ABOVE_ONE] = "no anonymous label above this one",
    [ZAP_E_NO_ANONYMOUS_LABELS_ALLOWED_IN_MAC] = "no anonymous labels allowed in a macro",
    [ZAP_E_NO_GLOBAL_LABELS_ALLOWED_IN_MACRO] = "no global labels allowed in a macro",
    [ZAP_E_NO_MODE_SUFFIX_CPU] = "no mode suffix on this CPU",
    [ZAP_E_NO_SUCH_INSTRUCTION_FORM] = "no such instruction form",
    [ZAP_E_ORG_GOES_BACKWARDS] = "org goes backwards",
    [ZAP_E_OUT_MEMORY] = "out of memory",
    [ZAP_E_OUT_MEMORY_LABELS] = "out of memory for labels",
    [ZAP_E_OUT_MEMORY_MACROS] = "out of memory for macros",
    [ZAP_E_OUT_MEMORY_OUTPUT] = "out of memory for the output",
    [ZAP_E_RELATIVE_JUMP_TOO_FAR] = "relative jump too far",
    [ZAP_E_STRING_NOT_TERMINATED] = "string not terminated",
    [ZAP_E_CONSTANT_TOO_LARGE_ADD_LABEL] = "that constant is too large to add to a label",
    [ZAP_E_MACRO_ALREADY_DEFINED] = "that macro is already defined",
    [ZAP_E_INSTRUCTION_NO_MODE_SUFFIX] = "this instruction takes no mode suffix",
    [ZAP_E_TOO_MANY_MACRO_ARGUMENTS] = "too many macro arguments",
    [ZAP_E_UNEXPECTED_TEXT_AFTER_INSTRUCTION] = "unexpected text after the instruction",
    [ZAP_E_UNKNOWN_INSTRUCTION] = "unknown instruction",
    [ZAP_E_UNKNOWN_LABEL] = "unknown label",
    [ZAP_E_UNSUPPORTED_CPU_TYPE] = "unsupported CPU type",
    [ZAP_E_WRONG_NUMBER_MACRO_ARGUMENTS] = "wrong number of macro arguments",
};

/* A code with no text prints nothing and looks like a message somebody forgot
 * to write, which is exactly what it is. The size catches one added at the
 * end; test_encode walks the table for the holes in the middle, which a
 * designated initialiser leaves as null. */
_Static_assert(sizeof(zap_err_text) / sizeof(zap_err_text[0]) == ZAP_E_COUNT,
               "every zap_err has an entry in zap_err_text");

/* What assemble_line does with a line before looking at it. */
#define LINE_ASSEMBLE 0
#define LINE_SKIP     1   /* a conditional is switched off */
#define LINE_CAPTURE  2   /* copying it into a macro being defined */

typedef struct symblock symblock;

/* Names live in blocks of this size; see namblock below for why. Declared here
 * because `dz` holds the list. */
#define NAMES_BLOCK 4096

typedef struct _namblock namblock;
struct _namblock {
    namblock* next;
    char buf[NAMES_BLOCK];
};

_Static_assert(NAMES_BLOCK > 255, "any single name has to fit in one block");

struct sym {
    const sym* next;

    /* The name, pointed at.
     *
     * It was an offset, because the arena behind it was one array grown with
     * realloc and a pointer into that would have to be rebased on every
     * growth. The arena is a list of blocks that never move now -- see
     * namblock -- so a pointer is both correct and one add cheaper on the
     * compare, which is the hottest loop the symbol table has. */
    const char* name;
    uint8_t len;

    /* A name is interned on first sight, defined or not, so a reference to a
     * label that has not appeared yet still gets an entry -- and the fixup
     * points at the entry rather than carrying another copy of the name.
     *
     * Copying it per reference was the first shape and it does not fit: the
     * labelled benchmark has 2,161 definitions and 4,318 references, and at
     * 26 characters each that is 152 KB of text for 56 KB of distinct names.
     * The Agon ran out of memory two thirds of the way through. Interning also
     * removes the lookup at resolve time; the address is simply there. */
    bool defined;

    /* Which table this node came from, and so which arena its name is in and
     * which fixup list a reference to it belongs on. Kept on the node rather
     * than on the operand because the operand is copied twice a line with an
     * ldir and this is written once per distinct label. */
    bool islocal;

    /* The value, which for an ordinary label is an address and for an EQU is
     * whatever was written. Four bytes and not the machine's three, because
     * the reference keeps four: `X: equ 0x55555555` then `dw32 X` is
     * 55555555 there, and `dl X` is 555555 with a "Value truncated to 24 bit"
     * warning -- so the truncation happens at the emitter, on the width the
     * directive asked for, and not when the name was defined.
     *
     * It costs a byte a symbol, which is the one place this file spends
     * memory rather than saving it. The alternative was to narrow here and it
     * is worse than it looks: `int` is three bytes on the Agon and four on the
     * host, so a truncating symbol would give **different bytes on the two
     * machines** for any EQU above 24 bits, which is the failure mode
     * everything else in this file is arranged to avoid. */
    evalue addr;
};

/* Buckets keyed by first character, last character and length.
 *
 * Not a hash: a hash is a walk over the name, and the scan that found the
 * token has already walked it. Measured over 25 real Agon programs -- 14,063
 * labels and 30,629 resolved references -- this touches 11.8 characters per
 * lookup against a Pearson hash's 17.5, because Pearson wins on probes and
 * loses on having to read the name twice. The workings are in
 * .internal/labels-plan.md.
 *
 * The first two characters would be the obvious key and are a bad one:
 * assembly labels cluster hard on prefixes -- ASC_TO_NUMBER1..4 -- so a
 * first-two-letters key leaves most buckets empty and runs chains of 67. It
 * is the *last* character and the length that discriminate.
 *
 * 2,048 buckets at four bytes is 8 KB. 8,192 was measured slightly better,
 * 10.8 characters against 11.8, and costs 32 KB; the smaller table is the
 * starting point and the trade is recorded rather than assumed. */
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

/* One bucket as it was before an expansion took it over. See scope_push. */
/* How deep INCLUDE and macro expansion may nest. Declared here because the
 * per-level expansion buffers are part of dz; the reasoning for the number is
 * where INCLUDE is. */
/* How deep INCLUDE and macro expansion may nest -- shared, because an
 * expansion is read the same way an included file is and re-enters the same
 * loop. Declared up here because the per-level expansion buffers are fields
 * of dz. */
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

/* Two ways of keying it, and the measurement that chose between them.
 *
 * Build with -DZAP_SYMHASH=0 for the other one. Both index the same 2,048
 * buckets, so the only difference is how the bucket is chosen and what
 * choosing it costs.
 *
 * The plan said the structural key -- first character, last character, length
 * -- because a hash is a walk over the name and the scan has already walked
 * it, and over 25 real programs it touches 11.8 characters a lookup against
 * Pearson's 17.5. Measured on the Agon, on seven sources of identical size,
 * that reasoning does not survive:
 *
 *     source                       structural   Pearson
 *     no labels at all                  1.60s     1.58s
 *     spread names, definitions         1.82s     1.72s
 *     spread names, backward refs       1.94s     1.96s
 *     spread names, forward refs        2.00s     2.02s
 *     four-character names              1.76s     1.60s
 *     fifteen characters, word list     1.92s     1.72s
 *     clustered names                   5.98s     1.84s
 *
 * Pearson is 1% worse on the two rows where the structural key is at its best
 * -- reference-heavy sources whose names spread over all three of its inputs
 * -- and better everywhere else, by 5 to 10% on ordinary names and by **69%**
 * on the last row. That row is 699 labels in one bucket, reached by naming
 * them `lbl_0001` upward, which is a convention rather than an attack.
 *
 * One percent against a factor of three is not a close decision. What the
 * average missed is that the tail is reachable by accident: this file's own
 * benchmark generator produced it on the first attempt.
 *
 * The comparison was run twice. The first Pearson table was `i * 167 + 13`,
 * which is a permutation -- 167 is odd, so it visits every value -- and a poor
 * hash, because a linear table leaves the rounds correlated: it used 234 of
 * the 2,048 buckets against a shuffle's 602. It still won by three times,
 * which says more about the key it replaced than about the table.
 *
 * The structural key is kept, callable and correct, because the argument for
 * it is sound and only the distribution defeats it -- if names are ever known
 * to be well spread it is the cheaper key. */
#ifndef ZAP_SYMHASH
#define ZAP_SYMHASH 1
#endif

#if ZAP_SYMHASH

/* A permutation of 0..255, which is what makes a Pearson hash a hash.
 *
 * It has to be a *shuffled* one. `i * 167 + 13` is a permutation too -- 167 is
 * odd, so it visits every value -- and it is a poor hash, because a linear
 * table leaves the rounds correlated: 700 labels of one stem used 234 of the
 * 2,048 buckets with a worst chain of 8, against 602 and 3 for a shuffle. It
 * still beat the key it replaced by three times, which says more about that
 * key than about this table.
 *
 * Built rather than written out, so there is no 256-byte literal in the
 * binary, and deterministic so both passes and every run agree. */
static uint8_t pearson[256];

static void build_pearson(void) {
    for (int i = 0; i < 256; i++) {
        pearson[i] = (uint8_t) i;
    }
    uint32_t seed = 12345;
    for (int i = 255; i > 0; i--) {
        seed = seed * 1103515245u + 12345u;
        const int j = (int) ((seed >> 16) % (uint32_t) (i + 1));
        const uint8_t t = pearson[i];
        pearson[i] = pearson[j];
        pearson[j] = t;
    }
}

/* One pass over the name, which is the whole cost of a Pearson key.
 *
 * A Pearson hash yields eight bits and the table wants eleven, and the obvious
 * way to find three more is a second pass seeded differently. That doubles the
 * per-character work -- and the character loop *is* the key's cost, measured at
 * 6.1% of runtime -- to buy a spread the table does not need.
 *
 * The three bits come from the first character, the last, and the length
 * instead. All three are already in hand, none of them costs a pass, and the
 * measurements say the spread is the same. Over the 7,684 distinct global
 * labels of the Agon corpus, expected probes for a successful lookup are 2.873
 * against the two-pass key's 2.882 -- marginally *better*, and inside the noise
 * of the sample either way. Per file, which is what an assembly run sees, both
 * are 1.000 and the worst file is 1.136 against 1.103.
 *
 * The length alone is not enough and the difference is not subtle: names of one
 * length then reach only 256 of the 2,048 buckets, which takes this
 * repository's own isa_memory benchmark -- every label four characters -- from
 * 4.478 probes to 28.427. XORing the two characters in is what rescues it, and
 * they cost two loads. */
static uint8_t pearson8(const char* name, int len) {
    uint8_t h = 0;
    const char* p = name;
    for (uint8_t k = (uint8_t) len; k != 0; k--) {
        h = pearson[h ^ (uint8_t) *p++];
    }

    return h;
}

/* Composed by writing the bytes rather than shifting: a shift by eight is
 * `call __ishl` on this chip. The compiler puts the shift back if the union is
 * written any other way, so this shape is load-bearing. */
static inline int sym_bucket(const char* name, int len) {
    union {
        int v;
        uint8_t b[sizeof(int)];
    } u;
    u.v = 0;
    u.b[0] = pearson8(name, len);
    u.b[1] = (uint8_t) ((name[0] ^ name[len - 1] ^ len) & 7);

    return u.v;
}

#else

/* The first character, the last and the length. Not a hash: a hash is a walk
 * over the name and the scan that found the token has already walked it.
 *
 * The index is `f * 64 + l * 2 + (len > 6)`, and written that way it was three
 * library calls: `* 64` is `call __ishl`, `len > 6` on a signed int is
 * `call pe, __setflag`, and the function itself was not inlined. Composed a
 * byte at a time from two small tables it is neither -- the same trick the hex
 * parser uses, for the same reason.
 *
 * f is five bits and l is five bits, so the low byte holds (f & 3) * 64 plus
 * l * 2 plus the length bit, which is at most 192 + 62 + 1, and the high byte
 * holds f >> 2. Both come from tables because a shift is a call. */
static const uint8_t f_lo[32] = {
    0, 64, 128, 192, 0, 64, 128, 192, 0, 64, 128, 192, 0, 64, 128, 192,
    0, 64, 128, 192, 0, 64, 128, 192, 0, 64, 128, 192, 0, 64, 128, 192,
};
static const uint8_t f_hi[32] = {
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7,
};

__attribute__((always_inline)) static inline int sym_bucket(const char* name,
                                                            int len) {
    const unsigned f = (unsigned) (uint8_t) name[0] & 31u;
    const unsigned l = (unsigned) (uint8_t) name[len - 1] & 31u;
    union {
        int v;
        uint8_t b[sizeof(int)];
    } u;
    u.v = 0;
    u.b[0] = (uint8_t) (f_lo[f] + l + l + ((unsigned) len > 6u ? 1u : 0u));
    u.b[1] = f_hi[f];

    return u.v;
}

#endif

/* A reference to a label that was not defined yet.
 *
 * zap makes one pass and never looks at a line twice, which is where its
 * speed comes from, so a forward reference cannot be resolved where it is
 * read. The output is held in memory in full, so it is patched at the end
 * instead -- which keeps the single pass and makes the cost of labels a line
 * item that can be measured on its own.
 */
typedef struct {
    const sym* target;  /* interned, so no name and no lookup to do */

    /* A second symbol, subtracted or added, or NULL. `end - start` with both
     * still ahead is 61% of the expressions that hold more than one forward
     * reference, and it is how a program measures a table it has not finished
     * writing.
     *
     * It costs nothing, because `next_addr` used to sit here and did not need
     * to: it was only ever read for a relative jump, and there it is always
     * `org + off + 1` -- the byte after the displacement byte, which `off`
     * already names. Three bytes that were being stored to say something the
     * record already knew.
     *
     * Next to `target` rather than after `addend` so that the record is a
     * power of two on the host too, where a pointer is eight bytes and an int
     * four; the static assert below is checked in both builds and only one
     * field order satisfies both. */
    const sym* sub;

    /* What to add to the address once it is known. `later + 4` is one symbol
     * and one constant, and that is what an expression over a forward label
     * comes to when the label appears once and only `+` and `-` connect it --
     * which is what 38% of the corpus's expressions look like. Anything else
     * is refused, because a fixup with one addend cannot represent it.
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

/* The range an addend has to fit, written out rather than derived from `int`,
 * which is three bytes on the Agon and four on the host. Deriving it would
 * make this refuse on one machine and accept on the other -- the failure this
 * whole widening exists to remove.
 *
 * Typed, and that is not decoration. Comparing an `evalue` against a bare
 * `int` constant compiled to a test of the wrong part of the value on the
 * Agon and refused `call @skip` -- an addend of zero -- while the host build
 * was correct. Both sides of every comparison here are the same width. */
#define ADDEND_MIN ((evalue) -0x800000L)
#define ADDEND_MAX ((evalue)  0x7FFFFFL)

/* Sixteen bytes, and the fourth byte of it is the point: `&list[i]` on a
 * thirteen-byte record is a call to __imulu, because the eZ80's multiply is
 * 8-bit. A power of two is a shift. The addend needed three bytes and the
 * padding was already being paid for in cache-free memory nobody was reading;
 * this spends it on something. */
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
 * Scopes end often -- once per global label, 1,941 of them in isa_real -- so
 * clearing 64 slots each time is 124,000 stores for a table that usually holds
 * two entries. The slot carries the scope it belongs to instead, and a slot
 * whose stamp is not the current one reads as empty however stale its chain.
 * Ending a scope is then an increment.
 *
 * The stamp goes in the byte that was padding: symslot needs one to make the
 * size a power of two, so this costs nothing at all. */
typedef struct {
    sym* head;
    uint8_t gen;
} locslot;

_Static_assert((sizeof(locslot) & (sizeof(locslot) - 1)) == 0,
               "local slot size is a power of two, so indexing is a shift");

typedef struct _dz {
    buf_reader rd;

    /* The output, as three pointers rather than a base and two offsets.
     *
     * `pos` was an int, so reserving room was `pos + 12 > cap` -- a signed
     * comparison, which the compiler cannot do in one subtract: it emitted
     * eleven instructions and a `call pe, __setflag` to fix the flags up on
     * overflow, once per instruction assembled. Against a limit held as a
     * pointer it is a load and an unsigned subtract, and the limit only moves
     * when the buffer does.
     *
     * It pays a second time at both ends of the emitter, which took its cursor
     * as `out + pos` and put it back as `o - out`. Held as a cursor there is
     * nothing to add and nothing to subtract. */
    uint8_t* out;   /* the buffer, for realloc and for writing it out */
    uint8_t* o;     /* the next byte to write */
    uint8_t* lim;   /* the last address at which a whole instruction still fits */

    /* Where the first byte of the output goes, which `ORG` may move.
     *
     * It was a compile-time constant, and every address in the assembler was
     * `ZAP_ORG + (o - out)` -- an immediate add. As a field it is a load first,
     * on every label definition, every `$`, every relative jump and every
     * fixup patched. That is what ORG costs whether or not a program uses it,
     * and it is why this sits next to the cursor it is always added to rather
     * than at the end with the cold fields.
     *
     * `org_set` is not "has ORG been seen" for its own sake: the first ORG in
     * a file relocates the origin and every later one pads out to its address,
     * and the reference distinguishes them exactly that way -- two ORGs with
     * nothing in between write the gap between them. */
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
     * than four; `ADL=1` is the eZ80's own. A file may switch as often as it
     * likes and the reference honours it per line, so this is a field and not
     * the compile-time constant it was. A forward reference records the width
     * it had when the instruction was emitted, which is what the fixup carries
     * -- so switching after the reference and before the definition changes
     * nothing, which is also what the reference does. */
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

    /* The line a global label was defined on, while the scope it opens has
     * not started yet; 0 when there is none pending.
     *
     * The reference resolves `two: jp @l` against the scope `two` closed, not
     * the one it opens: a label and an instruction on one line are two things,
     * and the operand is read before the scope moves. Ending the scope where
     * the label is defined instead makes that line refuse a local that the
     * reference assembles, and assemble one that it refuses -- the same bug
     * from both sides. */
    int scope_line;

    /* The local table: buckets, the blocks the nodes come from, their own name
     * arena, and the references waiting on a definition in this scope. Every
     * one of the used counters is reset when the scope ends; none of the
     * capacities are.
     *
     * LAST IN THE STRUCT, AND THAT IS NOT TIDINESS. dz is reached through a
     * pointer and `iy` displacement is a signed byte, so a field past 127 has
     * its address computed instead of being read in one instruction. The
     * buckets are 256 bytes on their own; put in the middle they pushed `line`
     * and `err` out of range, and `line` is written on every line of the
     * source. That cost 1.3% -- more than the whole feature -- for a table
     * this program touches only where a label is. */

    /* The file being read and how deep the includes go. `path` is what an
     * error message names, so it follows the reader down and back up; `depth`
     * is what stops a file that includes itself from taking the stack out.
     * Both are written once per file entered and read once per file left.
     *
     * Down here on principle rather than on evidence. They started beside
     * `org`, four bytes pushing every field after them along including the
     * symbol buckets, which looked like the explanation for INCLUDE costing
     * 0.8% on isa_memory. Moving them here changed the measurement by exactly
     * nothing -- 343/353/347/387 either way. They stay because a field written
     * once per *file* belongs with the cold ones, not because it was worth
     * anything. */
    const char* path;
    uint8_t depth;

    /* The run of reserved bytes the output currently ends with, if it ends
     * with one.
     *
     * `DS` and `ALIGN` reserve space rather than emit it, and the reference
     * only ever materialises that space when something is written after it: a
     * file ending in `DS 4` is four bytes shorter there, and `ALIGN` at the end
     * emits nothing at all. `ORG` is not the same and does pad -- all three
     * measured.
     *
     * Kept as where the run ends and how long it is, rather than as a flag on
     * every write. Only `emit_fill` touches these, so nothing on the path an
     * instruction takes has to know they exist, and the trailing run is
     * dropped once, at the end. */
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
     * opened it, which is the right lifetime while the file is being read and
     * the wrong one afterwards: a failure unwinds every one of those frames
     * and then reports, and `path` would name freed stack. It printed as
     * `inc_ .in`, which is how it was found.
     *
     * Copied here on the way out of a failure and nowhere else. The arena the
     * label names live in cannot be used -- it is grown with realloc and
     * moves. */
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
     * allocated per invocation: a malloc and a free were 2,200 cycles of the
     * 7,900 an expansion cost. The level in use is `depth`, which is why there
     * is one each -- an outer expansion is still being read from while an
     * inner one is built. */
    char* expbuf[INCLUDE_MAXDEPTH];
    int expcap[INCLUDE_MAXDEPTH];

    /* The arguments of the invocation being expanded at each depth. Here
     * rather than in a frame; see macro_args. */
    const char* margp[INCLUDE_MAXDEPTH * MACRO_MAXPARAM];
    int margn[INCLUDE_MAXDEPTH * MACRO_MAXPARAM];
} dz;

/* The one of these there is, at a fixed address.
 *
 * It was a local in main, reached everywhere through a `dz*` parameter -- so
 * every `zz.field` was `ld iy, (ix + 6)` to fetch the pointer out of the frame
 * and then an offset load through it, and there are 463 of those pointer
 * fetches in this translation unit with a handful on every line. A file-scope
 * object is addressed absolutely: the field's address is a constant the
 * assembler writes into the instruction, there is no base register to hold and
 * `iy` is freed for the line pointer that wants it.
 *
 * There is exactly one assembly per process, so this loses nothing that was
 * being used. Zero-initialised by being static, which is what the memset in
 * main did. */
static dz zz;

/* The failing line, copied out of whatever held it.
 *
 * Called only after something has returned false. Trailing space and the
 * newline come off, so the echo reads as the author wrote it, and a line
 * longer than the buffer is truncated -- the report is a courtesy and must
 * never itself be a failure.
 *
 * Returns nothing and cannot fail, for the same reason. */
static void err_line(char* dst, const char* p, const char* e) {
    int n = 0;
    while (p < e && *p != '\n' && n + 1 < ERRLINE_MAX) {
        dst[n++] = *p++;
    }
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\t'
                     || dst[n - 1] == '\r')) {
        n--;
    }
    dst[n] = 0;
}

/* The token a message is about, when the site that failed has it in hand.
 *
 * Not every one does -- an unresolved label is reported long after its line is
 * gone -- so this is set where it is cheap and true, and the report simply
 * leaves the quotation off where it is not. */
static void err_tok(const char* s, int n) {
    zz.errat = s;
    zz.erratlen = n;
}

/* The fields touched on every line have to be reachable in one instruction.
 *
 * dz is reached through a pointer and `iy` displacement is a signed byte, so a
 * field past 127 has its address computed instead. `line` is written once per
 * line of the source and the output cursor is read and written several times,
 * which is why those three are named here rather than the struct being trusted
 * to stay small: the local table alone is 256 bytes of buckets, and putting it
 * anywhere but the end pushes `line` out of range. That is what this catches,
 * and it caught it. */
/* The rule itself, which holds on any machine: the 256 bytes of local buckets
 * come after every field that is touched per line, not before them. */
_Static_assert(__builtin_offsetof(dz, locs) > __builtin_offsetof(dz, line),
               "the local table must come after the per-line fields");
_Static_assert(__builtin_offsetof(dz, locs) > __builtin_offsetof(dz, lim),
               "the local table must come after the output cursor");

/* And the displacement itself, where a displacement is what it is. The host
 * has eight-byte pointers and a dz twice the size, so the number only means
 * anything on the machine this is for. */
#ifdef AGONDEV
_Static_assert(__builtin_offsetof(dz, line) < 128, "dz.line is out of range");
_Static_assert(__builtin_offsetof(dz, o) < 128, "dz.o is out of range");
_Static_assert(__builtin_offsetof(dz, lim) < 128, "dz.lim is out of range");
_Static_assert(__builtin_offsetof(dz, org) < 128, "dz.org is out of range");
#endif

/* Marginal pricing of the label paths. Each duplicates a call to a function
 * that is already out of line, so nothing gets outlined by the measurement and
 * the difference is one extra execution. The data-only flags above are still
 * preferred where one exists for the thing being priced. */
#if defined(DUP_HASH) || defined(DUP_NUMTOK)
static volatile int dup_hash_sink;
#endif
#ifdef DUP_HASH
/* The name goes through a volatile pointer, or the duplicate is folded away:
 * sym_bucket is a pure function of what it is handed, so two calls on the same
 * arguments are one call, and the measurement would be of nothing. */
static const char* volatile dup_hash_name;
#define DUP_HASH_CALL(n, l) \
    do {                                     \
        dup_hash_name = (n);                 \
        dup_hash_sink = sym_bucket(dup_hash_name, (l)); \
    } while (0)
#else
#define DUP_HASH_CALL(n, l) ((void) 0)
#endif

/* ------------------------------------------------- symbols, continued */

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

/* The names, in blocks that never move, for the reason the nodes are.
 *
 * They were one array grown with realloc, and that is what a machine with
 * 512 KB and no virtual memory cannot afford: a realloc that has to move
 * holds the old block and the new one at once, so an arena of 110 KB needs
 * 228 KB to grow by eight. 14,616 labels ran out of memory at the 9,521st,
 * with the arena about two thirds of the way there.
 *
 * A block is never resized and never copied, so the peak is the total. A name
 * is at most 255 characters, so one always fits in a block and there is no
 * oversized case to write. */
/* Room for `len` characters in the newest block, or a new block.
 *
 * `used` is the offset within the newest one, so the blocks below it are full
 * and are never looked at again -- nothing walks this list except the free at
 * the end, and the local one, which rewinds it. */
static char* nam_take(namblock** head, int* used, int len) {
    namblock* b = *head;
    if (b == NULL || *used + len > NAMES_BLOCK) {
        Z_SITE("label names");
        b = (namblock*) malloc(sizeof(namblock));
        if (b == NULL) {
            return NULL;
        }
        b->next = *head;
        *head = b;
        *used = 0;
    }
    char* at = &b->buf[*used];
    *used += len;

    return at;
}

static bool sym_room(void) {
    if (zz.syms_used == SYMS_STEP) {
        Z_SITE("symbol blocks");
        symblock* b = (symblock*) malloc(sizeof(symblock));
        if (b == NULL) {
            return false;
        }
        b->next = zz.blocks;
        zz.blocks = b;
        zz.syms_used = 0;
    }

    return true;
}

/* Case-sensitive, unlike a mnemonic: the reference refuses `jp foo` against a
 * label written FOO. */
static const sym* sym_at(int b, const char* name, int len) {
    for (const sym* sp = zz.syms[b].head; sp != NULL; sp = sp->next) {
        if (sp->len != (uint8_t) len) {
            continue;
        }
        /* The pointer walk, which the local lookup has always used and this
         * one measured worse at -- when the name had to be computed as
         * `&zz.names[sp->nameoff]` and three live pointers would not fit where
         * two and an index did. The name is a pointer in the node now, so the
         * third one is free. */
        const char* t = sp->name;
        const char* q = name;
        const char* const qend = name + len;
        while (q != qend && *t == *q) {
            t++;
            q++;
        }
        if (q == qend) {
            return sp;
        }
    }

    return NULL;
}

/* The entry for a name, made if there is not one. */
static sym* sym_intern(const char* name, int len) {
    /* Hashed once. It was hashed twice: sym_find computed the bucket to search
     * and this computed it again to insert, so every name a source mentioned
     * for the first time was walked twice by the hash -- which is the whole
     * cost of a Pearson key. */
    DUP_HASH_CALL(name, len);
    const int b = sym_bucket(name, len);

#ifdef DUP_SYMCHAIN
    /* Data only: a decoy ahead of the real entry in the same bucket, same
     * length and differing in the last character, so the compare runs to the
     * end before failing. Doubles the chain walk; the real entry is still
     * found, so the output does not change. */
    char* dtext;
    if (sym_at(b, name, len) == NULL && sym_room()
        && (dtext = nam_take(&zz.names, &zz.names_used, len)) != NULL) {
        for (int i = 0; i < len; i++) {
            dtext[i] = name[i];
        }
        dtext[len - 1] = (char) (name[len - 1] == 'z' ? 'y' : 'z');
        sym* dec = &zz.blocks->nodes[zz.syms_used++];
        dec->name = dtext;
        dec->len = (uint8_t) len;
        dec->defined = false;
        dec->islocal = false;
        dec->addr = 0;
        dec->next = zz.syms[b].head;
        zz.syms[b].head = dec;
    }
#endif

    sym* found = (sym*) sym_at(b, name, len);
    if (found != NULL) {
        return found;
    }
    if (!sym_room()) {
        zz.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }

    char* text = nam_take(&zz.names, &zz.names_used, len);
    if (text == NULL) {
        zz.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }
    for (int i = 0; i < len; i++) {
        text[i] = name[i];
    }

    sym* sp = &zz.blocks->nodes[zz.syms_used++];
    sp->name = text;
    sp->len = (uint8_t) len;
    sp->defined = false;
    /* Set where the node is made, not where it is defined: a global that is
     * referenced before it is defined has to answer this the moment the
     * reference records a fixup against it. */
    sp->islocal = false;
    sp->addr = 0;

    sp->next = zz.syms[b].head;
    zz.syms[b].head = sp;

    return sp;
}

/* ------------------------------------------------------------ local labels */

/* Eight bits of the same key the global table uses. The high three bits it
 * composes are the ones this table does not have room for, so they are simply
 * not asked for; sym_bucket's low byte is the Pearson result itself. */
/* Six bits of the same pass, and none of the composing.
 *
 * It used to mask sym_bucket's answer, which built the eleven-bit key and then
 * threw five bits away -- and masking an `int` is `call __iand` here, so it
 * paid a library call for the privilege. The byte the pass produces is what
 * this wants; masking that is one instruction. */
static inline int loc_bucket(const char* name, int len) {
    return pearson8(name, len) & (NLOCB - 1);
}

/* Room for one more local node and its name. Blocks are threaded once and
 * then reused: after a scope ends loccur walks the same list again. */
static bool loc_room(void) {
    if (zz.locs_used == LOCS_STEP || zz.loccur == NULL) {
        locblock* next = zz.loccur != NULL ? zz.loccur->next : zz.locfirst;
        if (next == NULL) {
            Z_SITE("local label blocks");
            next = (locblock*) malloc(sizeof(locblock));
            if (next == NULL) {
                return false;
            }
            next->next = NULL;
            if (zz.loccur != NULL) {
                zz.loccur->next = next;
            } else {
                zz.locfirst = next;
            }
        }
        zz.loccur = next;
        zz.locs_used = 0;
    }

    return true;
}

/* Patches one reference, now that the address behind it is known. Shared by
 * the end of a scope, which settles that scope's local references, and the end
 * of the source, which settles every global one. */
static bool patch_fixup(const fixup* f) {
    const sym* sp = f->target;
    if (!sp->defined) {
        /* Reported against the line that used it, which is long gone; the
         * fixup carries the number for exactly this. The name is still on the
         * symbol, which is the whole reason a reference points at one. */
        zz.line = f->line;
        err_tok(sp->name, sp->len);
        zz.err = ZAP_E_UNKNOWN_LABEL;

        return false;
    }

    evalue val = sp->addr + f->addend;
    if (f->sub != NULL) {
        if (!f->sub->defined) {
            zz.line = f->line;
            err_tok(f->sub->name, f->sub->len);
            zz.err = ZAP_E_UNKNOWN_LABEL;

            return false;
        }
        val += (f->width & FIX_SUB2) ? -f->sub->addr : f->sub->addr;
    }

    const uint8_t w = (uint8_t) (f->width & FIX_WIDTH);
    uint8_t* at = zz.out + f->off;
    if (w == 0) {
        /* The byte after the displacement byte, which is where a relative
         * jump is measured from. */
        const evalue d = val - (zz.org + f->off + 1);
        if (d < -128 || d > 127) {
            zz.line = f->line;
            zz.err = ZAP_E_RELATIVE_JUMP_TOO_FAR;

            return false;
        }
        *at = (uint8_t) d;

        return true;
    }

    /* Same split as emit_data, and this loop runs once per forward reference
     * in the file -- 843 of them in isa_real, every one of them three bytes. */
    if (w > 3) {
        at[0] = (uint8_t) val;
        at[1] = (uint8_t) (val >> 8);
        at[2] = (uint8_t) (val >> 16);
        at[3] = (uint8_t) (val >> 24);

        return true;
    }
    const int v = (int) val;
    at[0] = (uint8_t) v;
    if (w > 1) {
        at[1] = (uint8_t) (v >> 8);
    }
    if (w > 2) {
        at[2] = (uint8_t) (v >> 16);
    }

    return true;
}

/* Ends the current scope: settles every local reference it left pending, then
 * empties the table.
 *
 * Every one of them has to settle here. A local reference can only be
 * satisfied inside its own scope, so one still undefined at this point is
 * undefined for good -- and reporting it here names the line that used it
 * while the scope it belonged to is still the subject, rather than at the end
 * of the source like a global.
 *
 * Emptying is three counters and an increment. The nodes and the names are
 * handed back to be written over, and the buckets are left exactly as they
 * are: the stamp is what makes them empty. */
/* Folds the local half of every fixup that names a global and a local.
 *
 * `end - @loop` puts the fixup on the global list, because that is where its
 * target belongs, and it is patched when the source runs out. By then the node
 * `@loop` points at has been handed back to the allocator and belongs to some
 * later scope's `@loop` -- so the subtraction came out against the wrong
 * address, with both addresses individually right.
 *
 * That is not hypothetical: it is three bytes of Rokky, `ld bc, vdp_cls-@cmd`,
 * where the local resolved against the last `@cmd` in the program. It cost
 * nothing to spot and would have shipped, because both assemblers accepted the
 * file and only 3 bytes of 31,520 differed.
 *
 * So the local half is settled here, where the node still means what it said,
 * and folded into the addend. What is left is an ordinary global fixup. */
static bool fold_subs(int from) {
    for (int i = from; i < zz.subfix_used; i++) {
        fixup* f = &zz.fixups[zz.subfix[i]];
        if (f->sub == NULL) {
            continue;
        }
        if (!f->sub->defined) {
            zz.line = f->line;
            err_tok(f->sub->name, f->sub->len);
            zz.err = ZAP_E_UNKNOWN_LABEL;

            return false;
        }
        const evalue sa = f->sub->addr;
        const evalue folded =
            (evalue) f->addend + ((f->width & FIX_SUB2) ? -sa : sa);
        if (folded < ADDEND_MIN || folded > ADDEND_MAX) {
            /* The local half of a global-minus-local, settled here. It is an
             * address difference in every real case and fits; an EQU wide
             * enough to leave the machine word does not, and says so. */
            zz.line = f->line;
            zz.err = ZAP_E_CONSTANT_TOO_LARGE_ADD_LABEL;

            return false;
        }
        f->addend = (int) folded;
        f->width &= (uint8_t) ~FIX_SUB2;
        f->sub = NULL;
    }
    zz.subfix_used = from;

    return true;
}

static bool scope_end(void) {
    if (!fold_subs(0)) {
        return false;
    }
    for (int i = 0; i < zz.lfix_used; i++) {
        if (!patch_fixup(&zz.lfixups[i])) {
            return false;
        }
    }
    zz.lfix_used = 0;
    zz.locs_used = LOCS_STEP;   /* forces loc_room back to the first block */
    zz.loccur = NULL;
    /* Back to the first block rather than freeing them: a scope ends on every
     * global label, and the blocks are the same size every time. */
    zz.locnames = zz.locnamfirst;
    zz.locnames_used = 0;
    if (++zz.gen == 0) {
        /* The stamp has wrapped, so a slot left over from 256 scopes ago would
         * read as belonging to this one. Once every 256 scopes, empty them
         * properly. */
        for (int b = 0; b < NLOCB; b++) {
            zz.locs[b].gen = 0;
            zz.locs[b].head = NULL;
        }
        zz.gen = 1;
    }

    return true;
}

/* Remembers one bucket, so that scope_pop can put it back. Out of line and
 * reached at most once per bucket per expansion. */
__attribute__((noinline))
static bool undo_note(int b) {
    if (zz.undo_used == zz.undo_cap) {
        /* Grown rather than capped. One entry per distinct bucket per level of
         * nesting is at most 64 times the nesting limit, but a body with sixty
         * local labels is legal -- the reference assembles one -- and a fixed
         * table would refuse it. */
        const int want = zz.undo_cap == 0 ? UNDO_STEP : zz.undo_cap + zz.undo_cap;
        Z_SITE("macro scope");
        locundo* grown = (locundo*) realloc(zz.undo, sizeof(locundo) * (size_t) want);
        if (grown == NULL) {
            zz.err = ZAP_E_OUT_MEMORY_MACROS;

            return false;
        }
        zz.undo = grown;
        zz.undo_cap = want;
    }
    locundo* u = &zz.undo[zz.undo_used++];
    u->head = zz.locs[b].head;
    u->b = (uint8_t) b;
    u->gen = zz.locs[b].gen;

    return true;
}

/* The entry for a local name in the current scope, made if there is not one.
 *
 * The bucket is empty unless its stamp is this scope's, whatever chain it
 * still holds from an earlier one -- those nodes have been handed back to the
 * allocator and may already be something else. */
static sym* loc_intern(const char* name, int len) {
    /* A scope a global label opened has to have started before this, and this
     * is the first moment it can matter: nothing but a local can tell the
     * difference. Asked here rather than on every line of the source, where it
     * cost 3.5% to answer a question only a line with an `@` on it can ask.
     *
     * On a later line than the label, though, and that is the whole point of
     * the deferral -- `two: jp @l` reads its operand in the scope `two` is
     * closing, not the one it opens, so the line the label was on is what has
     * to be compared and not merely whether there was one. */
    if (zz.scope_line != 0 && zz.scope_line != zz.line) {
        zz.scope_line = 0;
        if (!scope_end()) {
            return NULL;
        }
    }

    DUP_HASH_CALL(name, len);
    const int b = loc_bucket(name, len);
    if (zz.locs[b].gen == zz.gen) {
        for (sym* sp = zz.locs[b].head; sp != NULL; sp = (sym*) sp->next) {
            if (sp->len != (uint8_t) len) {
                continue;
            }
            const char* t = sp->name;
            const char* q = name;
            const char* const qend = name + len;
            while (q != qend && *t == *q) {
                t++;
                q++;
            }
            if (q == qend) {
                return sp;
            }
        }
    } else {
        /* The bucket belonged to an older scope and is about to belong to this
         * one. Inside an expansion the older scope is the caller's and is not
         * finished with, so what is here is written down first. */
        if (zz.expanding != 0 && !undo_note(b)) {
            return NULL;
        }
        zz.locs[b].gen = zz.gen;
        zz.locs[b].head = NULL;
    }

    if (!loc_room()) {
        zz.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }

    char* text = nam_take(&zz.locnames, &zz.locnames_used, len);
    if (text == NULL) {
        zz.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }
    if (zz.locnamfirst == NULL) {
        zz.locnamfirst = zz.locnames;
    }
    for (int i = 0; i < len; i++) {
        text[i] = name[i];
    }

    sym* sp = &zz.loccur->nodes[zz.locs_used++];
    sp->name = text;
    sp->len = (uint8_t) len;
    sp->defined = false;
    sp->islocal = true;
    sp->addr = 0;
    sp->next = zz.locs[b].head;
    zz.locs[b].head = sp;

    return sp;
}

/* Returns the symbol rather than a yes, because the caller has a use for it --
 * `name: EQU value` overwrites the address it was just given -- and because the
 * function already has it in hand. A pointer and a bool come back in the same
 * register here, so saying more costs nothing. */
__attribute__((always_inline))
static inline sym* sym_define(const char* name, int len, int addr) {
    sym* sp = sym_intern(name, len);
    if (sp == NULL) {
        return NULL;
    }
    if (sp->defined) {
        zz.err = ZAP_E_LABEL_DEFINED_TWICE;

        return NULL;
    }
    sp->defined = true;
    sp->addr = addr;

    return sp;
}

/* Writes an anonymous label here.
 *
 * It takes effect at once, which is what separates it from a global: `@@: jp
 * @b` jumps to itself, while `two: jp @l` still reads `@l` in the scope `two`
 * is closing. And `@f` on the same line means the *next* one, which falls out
 * of resolving the pending symbol before a new one is made for what follows. */
static bool anon_define(int addr) {
    zz.anon_prev = addr;
    zz.anon_has_prev = true;
    if (zz.anon_fwd != NULL) {
        zz.anon_fwd->defined = true;
        zz.anon_fwd->addr = addr;
        zz.anon_fwd = NULL;
    }

    return true;
}

/* The symbol every `@f` since the last `@@` is waiting on, made if there is
 * not one. Nameless and in no bucket: nothing ever looks it up, and the only
 * thing that finds it again is this field. */
static sym* anon_next(void) {
    if (zz.anon_fwd == NULL) {
        if (!sym_room()) {
            zz.err = ZAP_E_OUT_MEMORY_LABELS;

            return NULL;
        }
        sym* sp = &zz.blocks->nodes[zz.syms_used++];
        sp->next = NULL;
        sp->name = NULL;
        sp->len = 0;
        sp->defined = false;
        sp->islocal = false;
        sp->addr = 0;
        zz.anon_fwd = sp;
    }

    return zz.anon_fwd;
}

/* Defines a local in the current scope. Same shape as sym_define, against the
 * other table. */
/* Hands the symbol back, and stays inlined, for the reasons above. */
__attribute__((always_inline))
static inline sym* loc_define(const char* name, int len, int addr) {
    sym* sp = loc_intern(name, len);
    if (sp == NULL) {
        return NULL;
    }
    if (sp->defined) {
        zz.err = ZAP_E_LABEL_DEFINED_TWICE;

        return NULL;
    }
    sp->defined = true;
    sp->addr = addr;

    return sp;
}

/* Remembers a reference to a label that is not defined yet. The name is
 * copied for the same reason a definition's is: the line it came from is
 * gone by the time this is resolved. */
/* `addend` is the machine's word and not the evaluator's, which is what keeps
 * the mixed-width compare off the instruction path entirely: an operand's
 * immediate is an `int` and cannot be out of range. The one caller that can
 * hand over something wider is emit_data, and it checks before it calls. */
static bool fix_add(const sym* target, const sym* sub, int addend,
                    uint8_t width, int off) {
#ifdef NOFIX
    /* Prices the fixup list on its own: the immediate bytes are still written,
     * the reference to them is not recorded. The output is wrong and the
     * unresolved labels are never reported, which is why this is a measuring
     * build and nothing else. */
    (void) target; (void) sub; (void) addend; (void) width; (void) off;

    return true;
#endif
    /* A reference to a local goes on the scope's own list, because the node it
     * points at stops meaning this label the moment the scope ends. The flag
     * is on the node rather than on the operand that carried it here: the
     * operand is copied twice a line with an ldir and this is written once per
     * distinct label. */
    fixup** list = &zz.fixups;
    int* used = &zz.fix_used;
    int* cap = &zz.fix_cap;
    if (target->islocal) {
        list = &zz.lfixups;
        used = &zz.lfix_used;
        cap = &zz.lfix_cap;
    }

    if (*used == *cap) {
        Z_SITE("fixups");
        const int want = *cap + FIX_STEP;
        fixup* grown = (fixup*) realloc(*list, (size_t) want * sizeof(fixup));
        if (grown == NULL) {
            zz.err = ZAP_E_OUT_MEMORY_LABELS;

            return false;
        }
        *list = grown;
        *cap = want;
    }

    /* A fixup on the global list whose `sub` is a local has to be settled in
     * two halves; scope_end does the local one. Recorded by index because the
     * list is realloc'd out from under any pointer. */
    if (list == &zz.fixups && sub != NULL && sub->islocal) {
        if (zz.subfix_used == zz.subfix_cap) {
            Z_SITE("fixups");
            const int want = zz.subfix_cap == 0 ? 8 : zz.subfix_cap + zz.subfix_cap;
            int* grown = (int*) realloc(zz.subfix, (size_t) want * sizeof(int));
            if (grown == NULL) {
                zz.err = ZAP_E_OUT_MEMORY_LABELS;

                return false;
            }
            zz.subfix = grown;
            zz.subfix_cap = want;
        }
        zz.subfix[zz.subfix_used++] = *used;
    }

    fixup* f = &(*list)[(*used)++];
    f->target = target;
    f->sub = sub;
    f->addend = addend;
    f->width = width;
    f->off = off;
    f->line = zz.line;

    return true;
}

/* ---------------------------------------------------------------- output */

/* `need` is how many bytes the caller is about to write beyond the twelve an
 * instruction is allowed. A directive can ask for a whole string or a `DS` of
 * thousands, and growing 32 KB at a time until it fits would be a loop and a
 * realloc per step. */
static bool out_grow(int need) {
        Z_SITE("output buffer");
    /* Doubled, not stepped.
     *
     * A realloc that has to move holds the old block and the new one at once,
     * so the transient peak is what decides whether a growth fits rather than
     * the final size. Stepping by 32 KB reaches a 197 KB output through five
     * growths and the last of them asks for 192 + 224 = 416 KB; doubling
     * reaches it in two and the last asks for 128 + 256 = 384 KB. The peak
     * goes from about twice the final size to about 1.5 times it.
     *
     * `DB "row 1 of the table", 0` produces two thirds of a byte of output per
     * byte of source, against the fifth that the initial size below is
     * reckoned from -- the constant was set when the only thing a source could
     * hold was instructions. That is what made this reachable. */
    int want = zz.cap + (zz.cap < OUT_STEP ? OUT_STEP : zz.cap);
    const int least = (int) (zz.o - zz.out) + need + OUT_MAX_INSN;
    if (want < least) {
        want = least;
    }
    uint8_t* grown = (uint8_t*) realloc(zz.out, (size_t) want);
    if (grown == NULL) {
        /* The one failure that used to travel without a code: main printed
         * "out of memory for the output" when it found none set, which is a
         * fallback standing in for a message nobody had written. */
        zz.err = ZAP_E_OUT_MEMORY_OUTPUT;

        return false;
    }

    /* realloc is allowed to move the buffer, so the cursor and the limit are
     * both relative to a base that may no longer be there. */
    zz.o = grown + (zz.o - zz.out);
    zz.out = grown;
    zz.cap = want;
    zz.lim = grown + want - OUT_MAX_INSN;

    return true;
}

/* One `if`, not a loop: OUT_STEP is 32 KB and an instruction is at most 12
 * bytes, so one growth always leaves room. */
static bool out_reserve(void) {
    if (zz.o <= zz.lim) {
        return true;
    }

    return out_grow(0);
}

/* Room for `n` bytes, for the directives, which are the only things that write
 * more than an instruction's worth at once. Also one `if`, because out_grow
 * takes the amount and asks for it all in one go. */
static bool out_reserve_n(int n) {
    if (zz.o + n <= zz.lim) {
        return true;
    }

    return out_grow(n);
}

/* ------------------------------------------------------------- mnemonics */

#if defined(TRUNC) || defined(PTRUNC) || defined(LTRUNC) || defined(ETRUNC) \
    || defined(MTRUNC)
/* Where a truncated stage sinks what it computed, so the compiler cannot
 * delete the work whose result nothing reads. Declared before every cut, of
 * which there are now five kinds. */
static volatile int trunc_sink;
#endif

/* Mnemonics bucketed by first letter.
 *
 * zap reaches its instruction table through the same hash that holds every
 * reserved word, which on a machine with no cache is about a thousand cycles a
 * lookup. Here the first letter picks a bucket of four or five and the rest is
 * a length test and a compare. It is not a better idea in general -- it works
 * because the set is closed and small -- but it is what the floor should pay.
 */
/* Bucketed by first letter and length together.
 *
 * By first letter alone, a bucket held four or five and every one of them had
 * its length measured before it could be rejected -- which the compiler turned
 * into a strlen call per candidate, 85,407 of them and 8% of all work, to
 * re-derive something fixed at compile time. Lengths are taken once here, and
 * folding the length into the bucket leaves one or two candidates rather than
 * five.
 */
/* Marginal pricing of the table walks.
 *
 * Each DUP_ flag below writes one of the tables with every entry duplicated,
 * so the walk over it does twice the work and the program does nothing else
 * differently. A matching entry is still found at its first copy, so the
 * output is byte-identical -- which is the check that the measurement is
 * valid, and every bench run prints the md5 for it. The extra time is then
 * that walk's cost, with no instrumentation in the code being measured.
 *
 * That last part is the point. The same thing attempted by calling a function
 * twice from assemble_line measures something else: everything there is
 * inlined into one 3,500-line function, so a second call makes the compiler
 * outline it and the difference includes the outlining, paid on every line.
 * That showed up as a fixed 0.49s offset on match_row before the slope did.
 *
 *   make EXTRA_CFLAGS=-DDUP_ROW      the register test in match_row
 *   make EXTRA_CFLAGS=-DDUP_GROUP    the mode group walk
 *   make EXTRA_CFLAGS=-DDUP_BUCKET   the mnemonic bucket chain
 *
 * Measured on isa_real, 4.70s: 3.4%, 0.4% and 6.4%. test/run.sh builds all
 * three and checks the bytes are unchanged, because a flag that alters the
 * output prices nothing.
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

static rowinfo rowtab[NROW];

/* The rows of one mnemonic that share an operand mode.
 *
 * The rows used to be walked as a single list with a skip count, so rejecting
 * a mode cost a whole turn of the loop -- the counter test, moving the row
 * pointer into iy, loading ccok and the mode, then loading the skip and the
 * next pointer. Twenty instructions to learn that a group was the wrong shape.
 *
 * Lifting the modes out into their own table makes that a compare and a five
 * byte step, and the rows in the group no longer carry a mode test at all:
 * being in the group is the answer. The whole table is 170 of these.
 *
 * Sorting the rows by mode is what makes a group contiguous, and it is safe
 * because the mode test is an equality: rows outside the group can never
 * match, and a stable sort leaves the rows inside it in the order the table
 * gave them, so the first match is still the same row. */
typedef struct {
    uint8_t modes;
    uint8_t count;
    const rowinfo* rows;
} grpinfo;

static grpinfo grptab[NGRP];

/* One record per mnemonic, holding everything the hot path needs about it and
 * reached only by pointer.
 *
 * The lookup used to hand back an index, and every use of that index was an
 * array subscript: `isa_table[i].name` once per candidate examined,
 * `isa_table[idx]` and `rowtab[row_base[idx]]` once the mnemonic was known.
 * Each of those is the index times a struct size, and the eZ80's multiply is
 * 8-bit, so each is a call to __imulu.
 *
 * Chaining the buckets through pointers and carrying the row block as a
 * pointer leaves one subscript in the whole path -- the bucket head itself. */
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

static insninfo insntab[512];
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

static bucketslot bucket_head[NBUCKET];

/* What each row demands of the two operands' modes, in one value.
 *
 * Selecting a row asked `(row->condA & MODECHECK) == modeA` and the same for
 * B: four loads and two masks per candidate row, to compare against something
 * fixed when the table was generated. Both sides are folded here into one
 * 16-bit value per row, so the test becomes a single compare -- and rows are
 * scanned three or four deep for every instruction in the source.
 *
 * Indexed by a row number assigned here, since the rows live in 114 separate
 * arrays and have no global index of their own. */
/* One byte, not two.
 *
 * MODECHECK is four bits wide, so both operands' modes fit in a byte with room
 * to spare -- and a byte compare is native where a 16-bit one is the worst
 * width this chip has. This test runs for every candidate row, and `ld` alone
 * has 57 of them: "ld (ix+8), a" examines 43 before it matches. */

/* The same register sets the table holds, narrowed to the machine's word. */
/* Everything the row loop reads, in one record per row, walked by a pointer.
 *
 * Three separate problems led here. Holding the register sets as `uint24_t`
 * made `regset & reg` a call to __iand -- AND is an 8-bit instruction on this
 * chip -- and made indexing cost r * 3, a call to __imulu. Splitting them into
 * byte planes fixed both and was still slower (+8.5% on the row-heavy shape),
 * because eight separate arrays mean eight `ld hl, base; add hl, bc;
 * ld a, (hl)` sequences per row.
 *
 * One record indexed off iy is the shape that wins: every field is `ld a,
 * (iy+n)`, and advancing to the next row is a single lea. The fields stay
 * separate bytes rather than packed into a word -- packing two of them into a
 * uint16_t was tried and cost 18.8%, because ADL mode has no 16-bit truncation
 * and the compiler masks. Bytes are the native width for all of this. */


static bool tables_ready = false;

/* Shifting by a constant is a function call on this compiler.
 *
 * Only a shift by one becomes an instruction (`add a, a`); everything else --
 * `<< 3`, `<< 4`, `>> 8` on a 24-bit value -- is `ld b, n` and a call to
 * __bshl or __ishru. Writing the shift as repeated addition does not help,
 * because it is canonicalised back into a shift. A table does: an indexed load
 * from 256 bytes or fewer is one instruction, and these run for every operand
 * of every instruction in the source.
 *
 * The one shift the compiler does handle is extracting a byte from a wider
 * value when the result is cast to uint8_t -- `(uint8_t)(v >> 8)` is `ld a, h`
 * -- so emit_imm is written that way instead of with a loop. */
static const uint8_t shl3[8] = { 0, 8, 16, 24, 32, 40, 48, 56 };
static const uint8_t shl4[16] = {
    0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240
};

/* First letter to the base of its bucket run, folded into one table.
 *
 * This was `letter_of(first) * NLEN`: a case fold, two compares and a
 * multiply, and the multiply is a call to __imulu because the eZ80's MLT is
 * 8-bit and this is an int. All of it is a function of the character alone, so
 * all of it precomputes. 26 * 8 = 208 fits in a byte. */
/* Which characters begin a binary operator, as a table because cclass has no
 * bit left -- all eight are taken -- and because the question is asked once
 * per operand, right where the term ended. A compare chain of nine would be
 * paid by every operand that is not an expression, and 96.5% of them are not.
 *
 * `<` and `>` are here but must be doubled: the reference refuses a single
 * one, so `1<4` is an error rather than a comparison. */
static uint8_t exop[256];

/* Binding power per operator, and the switch that makes zap a drop-in for the
 * reference instead of an assembler that is right.
 *
 * The reference evaluates strictly left to right with no precedence at all:
 * `1+2*3` is 9 there, `2+3*4` is 20, `2*3+4*5` is 50. That is not a reading of
 * the source -- it is what the binary does, checked one expression at a time.
 * Every other assembler, and every reader, gives those 7, 14 and 26.
 *
 * So there are two tables and one algorithm. `-ez80` gives every operator the
 * same binding power, and a precedence climb where everything binds equally
 * *is* a left-to-right fold -- the compatible answer falls out of the same code
 * rather than needing its own.
 *
 * The default is the ordinary ordering -- C's, which is also every other
 * language's and what a reader of `mask & flag << 2` expects. It is not ZDS's
 * either: ZDS II 5.3.5, measured from its own listing, has only two levels,
 *
 *     1|6&4     4      (1|6)&4     where normal precedence gives 5
 *     6^3&2     0      (6^3)&2     and 4
 *     1<<2+1    5      (1<<2)+1    and 8
 *     4&3*2     4      4&(3*2)     agreeing here
 *
 * so `* /` bind tighter and `+ - << >> & | ^` are one level folding left to
 * right. Following that would be copying a second assembler's quirk to escape
 * the first one's. A ZDS program that relies on it needs brackets adding, and
 * that is a converter's job -- zds2ez80 has the corpus and the ZDS oracle to
 * do it with, and zap should be the assembler both of them can agree with
 * rather than a third dialect.
 *
 * Higher binds tighter. */
static uint8_t exprec[256];

/* Set once, from the command line, and read in the operator loop. A file-scope
 * flag rather than a field on dz, because dz is reached through a pointer on
 * every line and this is read only where an expression has an operator in it. */
static bool compat_ez80 = false;

/* Whether the error report is coloured. See is_color_opt. */
static bool use_color = false;

/* Which instruction set is in force, as the bitmask an isa_row carries.
 *
 * `.CPU Z80` and `.CPU Z180` are a *filter* in the reference, not a second
 * assembler: the same table, with the rows another machine does not have
 * taken out of it. The table here has carried the same bits since it was
 * written -- BIT_Z80, BIT_U80 for the undocumented Z80 forms, BIT_Z180,
 * BIT_EZ80 -- so honouring the directive is this mask and the two tests in
 * match_row that read it, and not a row of new work.
 *
 * A file-scope static rather than a field on `dz`, for the reason
 * `compat_ez80` is one: match_row is inlined into assemble_line and every
 * argument it takes is paid for on every instruction in the file. It is set
 * once per assembly at most.
 *
 * eZ80 until a directive says otherwise, which is what an Agon source is. */
static uint8_t cpu_mask = CPU_EZ80;

static uint8_t letter_base[256];

static inline int bucket_of(char first, int n) {
    return letter_base[(uint8_t) first] + (n < NLEN ? n : NLEN - 1);
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

__attribute__((noinline)) static void build_tables(void) {
#if ZAP_SYMHASH
    /* Here rather than in main, so that anything which sets the tables up gets
     * all of them. The unit tests call build_tables and build_cclass directly
     * and would have run with a table of zeros -- every name in one bucket,
     * still correct and quietly quadratic. */
    build_pearson();
#endif
    if (tables_ready) {
        return;
    }
    tables_ready = true;

    /* Built here rather than in build_cclass, which runs second: bucket_of
     * reads it and the bucket loop below is its first caller. */
    for (int i = 0; i < 256; i++) {
        letter_base[i] = 26 * NLEN;
    }
    for (int i = 0; i < 26; i++) {
        letter_base['a' + i] = (uint8_t) (i * NLEN);
        letter_base['A' + i] = (uint8_t) (i * NLEN);
    }

    for (int i = 0; i < NBUCKET; i++) {
        bucket_head[i].head = NULL;
    }
    /* Backwards, so each bucket ends up in table order. */
    for (int i = isa_table_count - 1; i >= 0; i--) {
        const char* name = isa_table[i].name;
        int k = 0;
        while (name[k] != 0) {
            /* same_ci folds the source and not the name, so a capital here
             * would make that mnemonic unmatchable and nothing else would say
             * why. The CLI test looks for this line. */
            if (name[k] >= 'A' && name[k] <= 'Z') {
                printf("isa table: %s is not lower case\r\n", name);
            }
            k++;
        }

        insninfo* ins = &insntab[i];
        ins->name = name;
        ins->len = (uint8_t) k;
        ins->count = (uint8_t) (isa_table[i].count * DUP_ROW_N);

        const int b = bucket_of(name[0], k);
        ins->next = bucket_head[b].head;
        bucket_head[b].head = ins;

#ifdef DUP_BUCKET
        /* A decoy ahead of the real entry in the same bucket, sharing its
         * first character and its length so the compare runs to the last
         * character before failing. Doubles the chain walk; the real entry is
         * still found, so the output does not change. */
        {
            static char decoy[512][8];
            insninfo* dec = &insntab[256 + i];
            for (int q = 0; q < k; q++) {
                decoy[i][q] = name[q];
            }
            decoy[i][k - 1] = (char) (name[k - 1] == 'z' ? 'y' : 'z');
            decoy[i][k] = 0;
            dec->name = decoy[i];
            dec->len = (uint8_t) k;
            dec->count = 0;
            dec->ngroups = 0;
            dec->rows = NULL;
            dec->groups = NULL;
            dec->next = bucket_head[b].head;
            bucket_head[b].head = dec;
        }
#endif
    }

    /* mnemonic_of compares n characters and does not check the length, which
     * is only safe while every name sharing a bucket has the same length --
     * otherwise `cp` would match the first two characters of `cpi`, and the
     * table is full of such prefixes. That holds because the bucket key
     * includes the length and no mnemonic is long enough to reach the clamp.
     * Both of those are somebody else's decision to change, so it is checked
     * here rather than assumed, and the CLI test looks for this line. */
    for (int b = 0; b < NBUCKET; b++) {
        for (const insninfo* x = bucket_head[b].head; x != NULL; x = x->next) {
            for (const insninfo* y = x->next; y != NULL; y = y->next) {
                if (x->len != y->len) {
                    printf("isa table: %s and %s share a bucket\r\n",
                           x->name, y->name);
                }
            }
        }
    }

    int r = 0;
    int g = 0;
    for (int i = 0; i < isa_table_count; i++) {
        const isa_insn* insn = &isa_table[i];
        const int base = r;
        const int gbase = g;
        insntab[i].rows = &rowtab[base];
        insntab[i].groups = &grptab[gbase];

        bool any_cc = false;
        for (int j = 0; j < insn->count; j++) {
            if ((insn->rows[j].flags & F_CCOK) != 0) {
                any_cc = true;
            }
        }

        for (int j = 0; j < insn->count; j++) {
            const isa_row* row = &insn->rows[j];
            rowinfo* ri = &rowtab[r];
            ri->modes = (uint8_t)
                (shl4[row->condA & MODECHECK] | (row->condB & MODECHECK));
            ri->ccok = (uint8_t) ((row->flags & F_CCOK) != 0);
            ri->a0 = (uint8_t) row->regsetA;
            ri->a1 = (uint8_t) (row->regsetA >> 8);
            ri->a2 = (uint8_t) (row->regsetA >> 16);
            ri->b0 = (uint8_t) row->regsetB;
            ri->b1 = (uint8_t) (row->regsetB >> 8);
            ri->b2 = (uint8_t) (row->regsetB >> 16);
            ri->aempty = (uint8_t) (row->regsetA == 0);
            ri->bempty = (uint8_t) (row->regsetB == 0);
            ri->row = row;
            r++;
#ifdef DUP_ROW
            /* The same row again. A matching row is found at its first copy so
             * the output is unchanged, while every row rejected on the way is
             * tested twice. */
            rowtab[r] = rowtab[r - 1];
            r++;
#endif
        }

        if (any_cc) {
            /* A row that takes a condition code can match with a mode that
             * does not, so it must be reached whatever the operands were.
             * call, jp, jr and ret are the four, ten rows between them, and
             * they keep the mode test on each row and scan linearly. Leaving
             * them ungrouped is what lets every other mnemonic drop the test. */
            insntab[i].ngroups = 0;

            continue;
        }

        /* Insertion sort, which is stable: rows sharing a mode keep the order
         * the table gave them, and the first match is unchanged. */
        for (int j = base + 1; j < r; j++) {
            const rowinfo tmp = rowtab[j];
            int k = j;
            while (k > base && rowtab[k - 1].modes > tmp.modes) {
                rowtab[k] = rowtab[k - 1];
                k--;
            }
            rowtab[k] = tmp;
        }

        insntab[i].groups = &grptab[g];
        for (int j = base; j < r; ) {
            int e = j;
            while (e < r && rowtab[e].modes == rowtab[j].modes) {
                e++;
            }
            if (g < NGRP) {
                grptab[g].modes = rowtab[j].modes;
                grptab[g].count = (uint8_t) (e - j);
                grptab[g].rows = &rowtab[j];
            }
            g++;
#ifdef DUP_GROUP
            /* The same group again. A group is found at its first copy so the
             * rows walked are unchanged, while every group rejected on the way
             * is rejected twice. */
            if (g < NGRP) {
                grptab[g] = grptab[g - 1];
            }
            g++;
#endif
            j = e;
        }
        insntab[i].ngroups = (uint8_t) (g - gbase);
    }

    /* Same reasoning as the bucket check above: a table that grows past this
     * would silently lose the groups that did not fit, and every mnemonic
     * after the overflow would stop matching. The CLI test looks for this
     * line. */
    if (g > NGRP) {
        printf("isa table: %d mode groups, NGRP is %d\r\n", g, NGRP);
    }
}

/* Two things this does not do, because the bucket already did them.
 *
 * It starts at 1. The bucket is chosen by letter_base[first], which maps 'a'
 * and 'A' to the same base, so every candidate in the chain already agrees
 * with the source on its first character -- comparing it again was a fifth of
 * the characters compared, at 4.15 per lookup over isa_real.
 *
 * And it folds one side, not two. Every name in the table is lower case,
 * checked when the table is built, so only the source needs the OR. */
static inline bool same_ci(const char* name, const char* s, int n) {
    /* Unsigned, and that is the whole of the change. Two signed ints compared
     * with `<` cannot be done in one subtract, so the compiler emitted a
     * `call pe, __setflag` to repair the flags on overflow -- inside the loop
     * that compares a mnemonic, on every line of the source. The same fault
     * the output cursor had when it was an offset and a capacity. */
    for (unsigned i = 1; i < (unsigned) n; i++) {
        if (name[i] != (s[i] | 0x20)) {
            return false;
        }
    }

    return true;
}

/* A whole-name compare including the first character, which `same_ci` skips
 * because its caller has already matched it through the bucket. */
/* The same, without the folding. Macro parameters are matched exactly. */
static bool same_full(const char* name, const char* s, int n) {
    for (int i = 0; i < n; i++) {
        if (name[i] != s[i]) {
            return false;
        }
    }

    return true;
}

static bool same_ci_full(const char* name, const char* s, int n) {
    for (int i = 0; i < n; i++) {
        if ((name[i] | 0x20) != (s[i] | 0x20)) {
            return false;
        }
    }

    return true;
}

/* The `.sis` / `.lil` after a mnemonic, as the S_* bit it selects.
 *
 * Two independent choices: whether the instruction runs in short or long mode,
 * and whether its immediates and addresses are short or long. A three-letter
 * suffix names both. The one- and two-letter forms name one and leave the
 * other as the current ADL mode, which is why this is not a plain lookup:
 * `.s` is `.sil` in ADL mode and `.sis` out of it.
 *
 *     .sis  0x40      .s   short instruction, immediates as they were
 *     .lis  0x49      .l   long instruction, immediates as they were
 *     .sil  0x52      .is  instruction as it was, short immediates
 *     .lil  0x5B      .il  instruction as it was, long immediates
 *
 * All eight spellings appear in the corpus. */
static bool suffix_bit(const char* t, int n, bool adl, uint8_t* out) {
    const char c0 = (char) (t[0] | 0x20);
    const char c1 = n > 1 ? (char) (t[1] | 0x20) : 0;
    const char c2 = n > 2 ? (char) (t[2] | 0x20) : 0;

    if (n == 1) {
        if (c0 == 's') { *out = adl ? S_SIL : S_SIS; return true; }
        if (c0 == 'l') { *out = adl ? S_LIL : S_LIS; return true; }

        return false;
    }
    if (n == 2) {
        if (c0 != 'i') {
            return false;
        }
        if (c1 == 's') { *out = adl ? S_LIS : S_SIS; return true; }
        if (c1 == 'l') { *out = adl ? S_LIL : S_SIL; return true; }

        return false;
    }
    if (n == 3 && c1 == 'i') {
        if (c0 == 's' && c2 == 's') { *out = S_SIS; return true; }
        if (c0 == 's' && c2 == 'l') { *out = S_SIL; return true; }
        if (c0 == 'l' && c2 == 's') { *out = S_LIS; return true; }
        if (c0 == 'l' && c2 == 'l') { *out = S_LIL; return true; }
    }

    return false;
}

/* The byte a suffix puts in front of the instruction. */
static uint8_t suffix_code(uint8_t bit) {
    if (bit == S_SIS) return CODE_SIS;
    if (bit == S_LIS) return CODE_LIS;
    if (bit == S_SIL) return CODE_SIL;

    return CODE_LIL;
}

/* Packing short names into a word and comparing them in one operation was
 * tried here and was 1.3% slower: bucketing by letter and length already
 * leaves one or two candidates, so the compare loop it replaced was two or
 * three characters long, and building the packed key cost more than that. */
/* always_inline, and the reason is the same one match_row carries. The suffix
 * reader looks a mnemonic up too, and that second caller was enough for the
 * compiler to stop inlining it into assemble_line -- where it is the lookup
 * every instruction line performs. Measured: isa_degenerate 4.86s to 5.16s,
 * for a feature that never runs on that file. */
__attribute__((always_inline))
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

/* ------------------------------------------------------- registers, flags */

/* Recognised straight from the text, with the bit and the index the encoder
 * wants. zap reaches these through a token type and then a switch; there is no
 * token here to carry one. */
/* Writes the register's bytes straight into the operand.
 *
 * It used to hand back a 24-bit mask that the caller split into bytes. The
 * split happens after the switch, where the value is no longer a constant, so
 * `bit >> 16` compiled to a call to __ishru -- on every register operand in
 * the source. Written inside each arm the shifts are constant expressions and
 * fold away, and the operand is a pointer the caller already had. */
#define SETREG(bits, idx)                        \
    do {                                         \
        op->r0 = (uint8_t) (bits);               \
        op->r1 = (uint8_t) ((bits) >> 8);        \
        op->r2 = (uint8_t) ((bits) >> 16);       \
        op->noreg = ((bits) == 0);               \
        op->reg_index = (idx);                   \
    } while (0)

static bool reg_of_text(const char* s, int n, dop* op, bool* is_cc,
                        uint8_t* cc_index) {
    *is_cc = false;
    *cc_index = 0;

    const char a = (char) (s[0] | 0x20);
    if (n == 1) {
        switch (a) {
            case 'a': SETREG(R_A, 7); return true;
            case 'b': SETREG(R_B, 0); return true;
            case 'c': SETREG(R_C, 1);
                      /* Carry has no name of its own: it would collide with
                       * register C, so the instruction decides which it meant. */
                      *is_cc = true; *cc_index = 3; return true;
            case 'd': SETREG(R_D, 2); return true;
            case 'e': SETREG(R_E, 3); return true;
            case 'h': SETREG(R_H, 4); return true;
            case 'l': SETREG(R_L, 5); return true;
            case 'i': SETREG(R_I, 0); return true;
            case 'r': SETREG(R_R, 0); return true;
            case 'z': SETREG(R_NONE, 0); *is_cc = true; *cc_index = 1; return true;
            case 'p': SETREG(R_NONE, 0); *is_cc = true; *cc_index = 6; return true;
            case 'm': SETREG(R_NONE, 0); *is_cc = true; *cc_index = 7; return true;
            default:  return false;
        }
    }

    if (n == 2) {
        const char b = (char) (s[1] | 0x20);
        switch (a) {
            case 'a': if (b == 'f') { SETREG(R_AF, 3); return true; } return false;
            case 'b': if (b == 'c') { SETREG(R_BC, 0); return true; } return false;
            case 'd': if (b == 'e') { SETREG(R_DE, 1); return true; } return false;
            case 'h': if (b == 'l') { SETREG(R_HL, 2); return true; } return false;
            case 's': if (b == 'p') { SETREG(R_SP, 3); return true; } return false;
            case 'm': if (b == 'b') { SETREG(R_MB, 0); return true; } return false;
            case 'i':
                if (b == 'x') { SETREG(R_IX, 2); return true; }
                if (b == 'y') { SETREG(R_IY, 2); return true; }

                return false;
            case 'n':
                if (b == 'z') { SETREG(R_NONE, 0); *is_cc = true; *cc_index = 0; return true; }
                if (b == 'c') { SETREG(R_NONE, 0); *is_cc = true; *cc_index = 2; return true; }

                return false;
            case 'p':
                if (b == 'o') { SETREG(R_NONE, 0); *is_cc = true; *cc_index = 4; return true; }
                if (b == 'e') { SETREG(R_NONE, 0); *is_cc = true; *cc_index = 5; return true; }

                return false;
            default: return false;
        }
    }

    /* af', which the table holds as plain R_AF -- the row for `ex af, af'` is
     * R_AF on both sides, so the apostrophe distinguishes nothing here and
     * only has to be accepted. */
    if (n == 3 && a == 'a' && (s[1] | 0x20) == 'f' && s[2] == '\'') {
        SETREG(R_AF, 3);

        return true;
    }

    if (n == 3 && a == 'i') {
        const char b = (char) (s[1] | 0x20);
        const char c = (char) (s[2] | 0x20);
        if (b == 'x') {
            if (c == 'h') { SETREG(R_IXH, 4); return true; }
            if (c == 'l') { SETREG(R_IXL, 5); return true; }
        } else if (b == 'y') {
            if (c == 'h') { SETREG(R_IYH, 4); return true; }
            if (c == 'l') { SETREG(R_IYL, 5); return true; }
        }
    }

    return false;
}

/* --------------------------------------------------------------- scanning */

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
 * with. Its own bit because the test used to be `name_ch(c) && !digit_ch(c)`,
 * which loads the same class byte twice and masks it twice, on every operand
 * in the source. */
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

static uint8_t cclass[256];

/* Nibble value of a hex digit, 0xFF for anything else. Used both to test a
 * digit and to convert it, so the character is classified once. */
static uint8_t hexval[256];

static void build_cclass(void) {
    for (int i = 0; i < 256; i++) {
        hexval[i] = 0xFF;
    }
    for (int i = 0; i < 10; i++) {
        hexval['0' + i] = (uint8_t) i;
    }
    for (int i = 0; i < 6; i++) {
        hexval['a' + i] = (uint8_t) (10 + i);
        hexval['A' + i] = (uint8_t) (10 + i);
    }
    for (int i = 0; i < 256; i++) {
        cclass[i] = 0;
    }
    cclass[(uint8_t) ' '] |= C_SPACE;
    cclass[(uint8_t) '\t'] |= C_SPACE;
    cclass[(uint8_t) '\r'] |= C_SPACE;

    for (int c = 'a'; c <= 'z'; c++) {
        cclass[c] |= C_NAME | C_NUM;
        cclass[c - 32] |= C_NAME | C_NUM;
    }
    for (int c = '0'; c <= '9'; c++) {
        cclass[c] |= C_NAME | C_DIGIT | C_NUM;
    }
    cclass[(uint8_t) '_'] |= C_NAME;

    /* A label may contain both, and an operand naming one has to scan the
     * whole of it. `_` had C_NAME but not C_NUM, so the literal scan -- which
     * is where a name that is not a register ends up -- stopped at the first
     * underscore and the rest of the line looked like trailing text. Real
     * labels are full of them. */
    cclass[(uint8_t) '_'] |= C_NUM;
    cclass[(uint8_t) '.'] |= C_NAME | C_NUM;

    /* The at sign, which marks a local label. The reference allows it anywhere
     * in a name -- `ab@cd:` is a global there -- and only a leading one makes
     * a label local, so it is an ordinary name character here too and the
     * leading position is what parse_operand and the definition path test. */
    cclass[(uint8_t) '@'] |= C_NAME | C_NUM;

    /* A mnemonic runs over its suffix too, so the dot belongs to the same run
     * -- asking for it separately made the scan two tests per character. */
    for (int i = 0; i < 256; i++) {
        if (cclass[i] & C_NAME) {
            cclass[i] |= C_MNEM;
        }
    }
    cclass[(uint8_t) '.'] |= C_MNEM;
    cclass[(uint8_t) ','] |= C_OPEND;
    cclass[(uint8_t) '\n'] |= C_OPEND;
    cclass[(uint8_t) ';'] |= C_OPEND;
    cclass[(uint8_t) '('] |= C_LPAREN;
    for (int i = 0; i < 256; i++) {
        if ((cclass[i] & C_NAME) != 0 && (cclass[i] & C_DIGIT) == 0) {
            cclass[i] |= C_ALPHA;
        }
    }
    /* The dot is a name character but must not start one: `.5` is not a label
     * and a mnemonic suffix is scanned as part of the mnemonic. */
    cclass[(uint8_t) '.'] &= (uint8_t) ~C_ALPHA;

    /* Nor may the at sign, and here that is a saving rather than a rule: an
     * operand starting with one is a local label and cannot be a register, so
     * leaving it out of C_ALPHA sends it straight to the path that reads a
     * name and looks it up, past reg_of_text entirely. */
    cclass[(uint8_t) '@'] &= (uint8_t) ~C_ALPHA;
    for (int i = 0; i < 256; i++) {
        exop[i] = 0;
    }
    {
        const char* o = "+-*/<>&|^";
        for (int i = 0; o[i] != 0; i++) {
            exop[(uint8_t) o[i]] = 1;
        }
    }
    for (int i = 0; i < 256; i++) {
        exprec[i] = 0;
    }
    if (compat_ez80) {
        /* One level, so the climb below folds left to right and agrees with
         * the reference. */
        const char* o = "+-*/<>&|^";
        for (int i = 0; o[i] != 0; i++) {
            exprec[(uint8_t) o[i]] = 1;
        }
    } else {
        exprec[(uint8_t) '*'] = 6;
        exprec[(uint8_t) '/'] = 6;
        exprec[(uint8_t) '+'] = 5;
        exprec[(uint8_t) '-'] = 5;
        exprec[(uint8_t) '<'] = 4;   /* << */
        exprec[(uint8_t) '>'] = 4;   /* >> */
        exprec[(uint8_t) '&'] = 3;
        exprec[(uint8_t) '^'] = 2;
        exprec[(uint8_t) '|'] = 1;
    }

    cclass[(uint8_t) '$'] |= C_NUM;
    cclass[(uint8_t) '#'] |= C_NUM;
    cclass[(uint8_t) '%'] |= C_NUM;
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

/* One operand.
 *
 * A whole expression evaluator is a feature: this takes a register, a flag, a
 * literal, or a parenthesised form of those with an optional displacement, and
 * nothing else. Arithmetic between literals is one of the things to add back
 * and price later.
 */
/* An operand with nothing in it, to copy from.
 *
 * Clearing ten fields by hand is ten stores, twice for every line in the
 * source -- and two of them are the four-byte immediate and displacement,
 * which on a 24-bit machine are not one store each. Copying a prepared struct
 * lets the compiler move it in whatever way suits, and says once what "empty"
 * means instead of in three places that have to agree. */
static const dop dop_none = {
    /* By name, not by position. Adding a field to dop silently shifted every
     * value after it once already: noreg took indirect's initialiser and the
     * operand came out claiming to hold a register. Nothing about that is
     * visible at the point of the mistake. */
    .r0 = 0, .r1 = 0, .r2 = 0,
    .noreg = 1,
    .reg_index = 0,
    .cc = false,
    .cc_index = 0,
    .mode = NOREQ,
    .disp = 0,
    .fwd = NULL,
    .fwd2 = NULL,
    .fwd2_neg = false,
    .imm = 0,
};

/* A run of hexadecimal digits, assembled into a value.
 *
 * Assembled from the end, a byte at a time, rather than accumulated as
 * `acc = (acc << 4) | digit`.
 *
 * There is no barrel shifter, and the compiler will not turn a left shift into
 * a byte move even at a byte boundary: `<< 4` and `<< 8` are both
 * `ld c, n; call __ishl`, a loop over the bits. That cost about 445 cycles per
 * hex digit, which is why `ld hl, 0x123456` timed 2.9s slower than
 * `ld a, 0x42` over thirty thousand lines.
 *
 * Working backwards, two digits make a byte with one table lookup for the high
 * nibble, and the bytes go straight into the value's own storage. Nothing here
 * shifts anything wider than a nibble.
 *
 * Three fixed steps rather than a loop with a running byte index. A value is
 * at most three bytes, so the loop could only ever run three times, and it was
 * paying for that: a counter to increment, a bound to test against it, and an
 * indexed store into the union, which is address arithmetic on every byte.
 *
 * The digits are checked here too, rather than in a pass of their own. Each
 * used to be looked up twice -- once by a loop asking whether the run was hex,
 * and again here -- and that pass cost 811 cycles of this parse's 2,163, and
 * 393 even for `ld a, 0x42`, where there are two digits. Most of it was the
 * loop, not the work.
 *
 * hexval gives 0xFF for anything that is not a hex digit and a real nibble is
 * 0x0F or less, so OR-ing the nibbles together and testing the high half at
 * the end says whether any was rejected, with no branch per digit.
 *
 * Little-endian, which the eZ80 is and the host is. The emitter already writes
 * the low byte first for the same reason. A run longer than the machine's word
 * is declined rather than truncated, and the reason is at the test.
 *
 * Taking the run rather than the whole token is what lets `0x1234` and
 * `1234h` share this. They used to be different code: the prefixed form came
 * here and the suffixed form fell through to num_parse, which on the honest
 * corpus is 46% of every immediate in isa_real. */
static inline bool hex_digits(const char* d, int n, int* out) {
    /* Six digits, which is the machine's word, and anything longer is handed
     * to num_parse instead of read here.
     *
     * This used to keep four bytes so that `dw32 0x55555555` could come
     * through the fast path. It cost **0.10s of isa_real** -- every literal
     * in the file assembled into a wider union and moved out through a wider
     * variable, to buy the one form in the corpus that needs it. Declining is
     * free: lit_value returns NULL and the caller falls to the evaluator,
     * whose atom reaches num_parse, which has been 32-bit all along and reads
     * every radix. The fast path exists for what is common, and an eight-digit
     * hex literal is not.
     *
     * Six and not seven: seven digits is 28 bits and does not fit either. */
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

/* Whether a token could be a number at all, decided on two characters.
 *
 * Every radix this assembler takes is marked at one end or the other: a
 * leading digit for decimal, 0x and 0b; a leading $, # or %; a trailing h or
 * b. A token with none of those is a name and there is nothing to work out.
 *
 * It is only a filter. What it lets through still has to be parsed, because a
 * leading digit does not make a number -- 2 is not a binary digit, so `2b` is
 * a name, and so are `1z`, `5g` and `123abc`. The reference takes all four as
 * labels, and assuming otherwise was wrong for four of twenty probes.
 *
 * What the filter buys is the common path: an ordinary label neither starts
 * with a digit nor ends in h or b, so it never reaches the general parser.
 * Label definitions are 15.5% of the time on isa_real and every one of them
 * was calling it. */
static inline bool maybe_numeric(const char* s, int n) {
    const char f = s[0];
    const char last = (char) (s[n - 1] | 0x20);

    return digit_ch(f) || f == '$' || f == '#' || f == '%'
           || last == 'h' || last == 'b';
}

static bool numeric_token(const char* s, int n) {
    if (!maybe_numeric(s, n)) {
        return false;
    }

    /* The fast reader first, and num_parse for whatever it declines --
     * *falling through* rather than returning its answer.
     *
     * Returning it was right while hex_digits read everything the reference
     * calls a hex literal. It stopped being right when hex_digits began to
     * decline a run wider than the machine's word: `0x55555555` was then
     * "not a number", so the operand parser took it for a label, interned it
     * and made it a forward reference to a name nothing ever defines. The
     * fast path declining has to mean "ask the slow one", never "no". */
    int v = 0;
    if (n >= 3 && s[0] == '0' && (s[1] | 0x20) == 'x') {
        if (hex_digits(s + 2, n - 2, &v)) {
            return true;
        }
    } else if (n >= 2 && (s[n - 1] | 0x20) == 'h') {
        if (hex_digits(s, n - 1, &v)) {
            return true;
        }
    }
    value gv = 0;

    return num_parse(s, n, &gv);
}

/* ------------------------------------------------------------- expressions */

/* Defined with the directives, because that is where strings are written out.
 * A character literal uses the same table; see the comment there. */
static int str_escape(char c);


/* The reference evaluates strictly left to right with no precedence at all:
 * it keeps a running total and folds each term into it as the term arrives.
 * `1+2*3` is 9 there and 7 nowhere, and matching that is the whole job -- an
 * evaluator that got precedence "right" would disagree with the assembler this
 * one exists to agree with.
 *
 * Measured over the Agon corpus, 3.46% of operands hold an expression and 73%
 * of those have exactly one operator, so the shape to be fast on is `label+1`
 * and `end-start` rather than anything that needs a stack.
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

/* Nesting is counted here rather than passed down, because a bracket recurses
 * through expr_term and expr_value and both of those run on every term. A
 * parameter neither of them otherwise wants would be paid for by every operand
 * in the file; a byte in memory is paid for by the brackets alone.
 *
 * It also has to be counted somewhere. `depth` below bounds the precedence
 * climb and only that: a bracket goes expr_term -> expr_value -> expr_climb,
 * and expr_value starts a fresh climb at depth zero, so nesting was unbounded
 * despite the paragraph above claiming otherwise. 5,000 deep is fine on a host
 * with an eight-megabyte stack, which is why no test caught it. */
static uint8_t expr_depth;

/* The forward references an expression is carrying, and whether they are still
 * in a shape a fixup can hold.
 *
 * A fixup holds two symbols and a constant, and adds the first: it can
 * represent `k + a`, `k + a - b` and `k + a + b`, and nothing else. So an
 * expression may name at most two labels that are not defined yet, each may
 * only be joined to the rest by `+` and `-`, and at least one of the two has
 * to end up added.
 *
 * A sign is kept per symbol rather than a single "is this still usable" flag,
 * which is what the one-symbol version had. That is what makes `end - start`
 * work, and it also picked up `k - later` for free -- previously refused,
 * because with nowhere to write a sign the only safe answer was no.
 *
 * Tracked here rather than returned, because returning them would widen every
 * signature in the evaluator including the recursive one. Reset per operand,
 * in parse_operand, which is the only place an expression starts.
 *
 * Two named slots rather than an array: a `const sym*` is three bytes, so a
 * variable subscript into an array of them is a call to __imulu on every
 * lookup. Two is few enough to spell out. */
static const sym* expr_fwd;     /* slot 0, bit 0 of a mask */
static const sym* expr_fwd2;    /* slot 1, bit 1 */
static bool expr_fwd_neg;
static bool expr_fwd2_neg;
static bool expr_fwd_bad;

/* Which slots hold a symbol. A term reports the ones it filled by taking this
 * before and after itself, so a bracketed sub-expression needs no special
 * case: whatever it left behind belongs to the term that contained it. */
static uint8_t fwd_live(void) {
    uint8_t m = 0;
    if (expr_fwd != NULL) {
        m = 1;
    }
    if (expr_fwd2 != NULL) {
        m |= 2;
    }

    return m;
}

/* Claim a slot for a symbol that is not defined yet. A third one has nowhere
 * to go, and saying so here is the only place that has to know the limit. */
static void fwd_take(const sym* sp) {
    if (expr_fwd == NULL) {
        expr_fwd = sp;
        expr_fwd_neg = false;
    } else if (expr_fwd2 == NULL) {
        expr_fwd2 = sp;
        expr_fwd2_neg = false;
    } else {
        expr_fwd_bad = true;
    }
}

/* Subtraction, and unary minus, flip the sign of everything on their right. */
static void fwd_negate(uint8_t mask) {
    if (mask & 1) {
        expr_fwd_neg = !expr_fwd_neg;
    }
    if (mask & 2) {
        expr_fwd2_neg = !expr_fwd2_neg;
    }
}

/* What survived, arranged the way a fixup wants it: the added symbol first.
 *
 * `start - end` and `end - start` differ only in which slot the minus landed
 * on, so the ordering is decided here rather than by the order they were
 * written in. Two negatives is the one two-symbol shape that cannot be
 * represented, because the fixup always adds its first symbol. */
/* always_inline, and it has to be. `fwd_finish` runs on every expression
 * operand in the file, and giving this a second caller in the directives was
 * enough for the compiler to outline it -- a call with three out-parameters on
 * every forward reference. Fourth time this file has met that. */
__attribute__((always_inline))
static inline bool fwd_result(const sym** target, const sym** sub,
                              bool* subneg) {
    *sub = NULL;
    *subneg = false;
    if (expr_fwd_bad) {
        zz.err = ZAP_E_LABEL_DEFINED_ALREADY;

        return false;
    }
    if (expr_fwd2 == NULL) {
        if (expr_fwd_neg) {
            zz.err = ZAP_E_LABEL_CANNOT_NEGATED;

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
        zz.err = ZAP_E_LABEL_CANNOT_NEGATED;

        return false;
    }

    return true;
}

/* The same answer written onto an operand.
 *
 * The data directives want the ordering too and have no operand to write it
 * onto, which is why it is a function of its own rather than the tail of this
 * one. */
/* Keeps an expression that could not be reduced to what a fixup carries.
 *
 * A fixup holds two symbols and a constant, which covers `end - start + 4` and
 * every shape an assembler is usually asked for. `TENDIF*256+TTHEN` is not one
 * of them, and it is real: BBC BASIC builds a two-byte token pair that way,
 * eleven times, from EQUs defined in a file included after the one using them.
 * A second pass resolves it; one pass has to keep something.
 *
 * What it keeps is the text, and a nameless symbol to stand for its value.
 * Nothing else has to change: the operand carries that symbol as an ordinary
 * forward reference, the emitter makes an ordinary fixup out of it, and when
 * the source runs out the text is evaluated -- with everything now defined --
 * and the symbol given its answer before any fixup is patched.
 *
 * Nameless and in no bucket, exactly as `@f`'s pending symbol is, so nothing
 * can find it by name. */
/* Defined below, with the rest of the forward-reference slots. */
static void fwd_reset(const sym* seed);

__attribute__((noinline))
static sym* defer_text(const char* text, int n) {
    if (!sym_room()) {
        zz.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }
    if (zz.defer_used == zz.defer_cap) {
        Z_SITE("deferred expressions");
        const int want = zz.defer_cap == 0 ? 8 : zz.defer_cap + zz.defer_cap;
        defexpr* grown =
            (defexpr*) realloc(zz.defer, (size_t) want * sizeof(defexpr));
        if (grown == NULL) {
            zz.err = ZAP_E_OUT_MEMORY_LABELS;

            return NULL;
        }
        zz.defer = grown;
        zz.defer_cap = want;
    }
    char* copy = nam_take(&zz.names, &zz.names_used, n + 1);
    if (copy == NULL) {
        zz.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }
    for (int i = 0; i < n; i++) {
        copy[i] = text[i];
    }
    copy[n] = 0;

    sym* sp = &zz.blocks->nodes[zz.syms_used++];
    sp->next = NULL;
    sp->name = NULL;
    sp->len = 0;
    sp->defined = false;
    sp->islocal = false;
    sp->addr = 0;

    defexpr* d = &zz.defer[zz.defer_used++];
    d->sp = sp;
    d->text = copy;
    d->len = n;
    d->line = zz.line;
    zz.err = ZAP_OK;
    fwd_reset(NULL);

    return sp;
}

/* The same for an operand, which carries the symbol as its forward
 * reference and lets the emitter make an ordinary fixup out of it. */
static bool defer_expr(const char* text, int n, dop* op) {
    sym* sp = defer_text(text, n);
    if (sp == NULL) {
        return false;
    }
    op->fwd = sp;
    op->fwd2 = NULL;
    op->fwd2_neg = false;
    op->imm = 0;

    return true;
}

static bool fwd_finish(dop* op) {
    if (expr_fwd == NULL) {
        return true;
    }

    const sym* target = NULL;
    const sym* sub = NULL;
    bool subneg = false;
    if (!fwd_result(&target, &sub, &subneg)) {
        return false;
    }
    op->fwd = target;
    op->fwd2 = sub;
    op->fwd2_neg = subneg;

    return true;
}

static void fwd_reset(const sym* seed) {
    expr_depth = 0;
    expr_fwd = seed;
    expr_fwd2 = NULL;
    expr_fwd_neg = false;
    expr_fwd2_neg = false;
    expr_fwd_bad = false;
}

static bool expr_value(evalue* out, const char** pp, const char* e,
                       uint8_t* fwdmask);
static bool expr_climb(evalue* total, const char** pp, const char* e,
                       uint8_t minprec, int depth, uint8_t* fwdmask);

/* A bare token inside an expression: a number in any radix the reference takes,
 * or a label that is already defined.
 *
 * Already defined is the limit of this stage. A forward reference on its own is
 * still a fixup and still works -- `jp later` is untouched -- but one inside an
 * expression would need the fixup to carry the rest of the sum, which is the
 * next stage and is refused here rather than guessed at. 35.7% of the corpus's
 * expressions need nothing more than this. */
static bool expr_atom(evalue* out, const char* ns, int nn) {
    /* The machine's word: a run this declines falls through to num_parse
     * below, which is where a literal wider than the machine is read. */
    int v = 0;
    if (nn >= 3 && ns[0] == '0' && (ns[1] | 0x20) == 'x') {
        if (hex_digits(ns + 2, nn - 2, &v)) {
            *out = v;

            return true;
        }
    } else if (nn >= 2 && (ns[nn - 1] | 0x20) == 'h') {
        if (hex_digits(ns, nn - 1, &v)) {
            *out = v;

            return true;
        }
    }

    /* Decimal, read here rather than through num_parse.
     *
     * A token that is nothing but digits is a number and can be nothing else,
     * so neither the check nor the general parser has anything to decide. Both
     * are real calls, and between them they were most of what `DS 4` cost:
     * 7,520 cycles against `DB 4`'s 3,281, for the same one-digit number
     * through the evaluator instead of the data path's fast read.
     *
     * The same accumulation as lit_value, deliberately: the first digit
     * outside the loop, because `acc * 10` is a call to __imulu here, and the
     * same 24-bit wrap on a value too big to fit, so that `DB 20000000` and
     * `DS 20000000` cannot disagree with each other. */
    if (digit_ch(ns[0]) && nn <= 6) {
        /* Six digits, for the reason at hex_digits: inside the machine's word,
         * so this is narrow arithmetic and anything longer falls to num_parse
         * below rather than being read twice as wide. */
        int acc = ns[0] - '0';
        int k = 1;
        for (; k < nn; k++) {
            if (!digit_ch(ns[k])) {
                break;
            }
            acc = acc * 10 + (ns[k] - '0');
        }
        if (k == nn) {
            *out = acc;

            return true;
        }
    }

    if (!numeric_token(ns, nn)) {
        const sym* sp = NULL;
        if (ns[0] == '@') {
            const char k = nn == 2 ? (char) (ns[1] | 0x20) : 0;
            if (k == 'b' || k == 'p') {
                if (!zz.anon_has_prev) {
                    zz.err = ZAP_E_NO_ANONYMOUS_LABEL_ABOVE_ONE;

                    return false;
                }
                *out = zz.anon_prev;

                return true;
            }
            if (k == 'f' || k == 'n') {
                sp = anon_next();
                if (sp == NULL) {
                    return false;
                }
                fwd_take(sp);
                *out = 0;

                return true;
            }
            sp = loc_intern(ns, nn);
        } else {
            sp = sym_intern(ns, nn);
        }
        if (sp == NULL) {
            return false;
        }
        if (!sp->defined) {
            /* Not known yet. Two of these an expression may have; a third has
             * nowhere to go. */
            fwd_take(sp);
            *out = 0;

            return true;
        }
        *out = sp->addr;

        return true;
    }

    value gv = 0;
    if (!num_parse(ns, nn, &gv)) {
        zz.err = ZAP_E_EXPECTED_VALUE;

        return false;
    }
    *out = gv;

    return true;
}

/* One term, with any unary operators in front of it. `fwdmask` comes back
 * holding the slots this term put a forward reference into, so its caller
 * knows which signs its operator has to flip. */
static bool expr_term(evalue* out, const char** pp, const char* e,
                      uint8_t* fwdmask) {
    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }

    /* Unary operators stack the way the reference allows them: it takes one,
     * so `--5` is an error there and here. */
    int sign = 1;
    bool invert = false;
    if (*p == '-' || *p == '+' || *p == '~') {
        if (*p == '-') {
            sign = -1;
        } else if (*p == '~') {
            invert = true;
        }
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p == '-' || *p == '+' || *p == '~' || exop[(uint8_t) *p] != 0) {
            zz.err = ZAP_E_UNARY_OPERATOR_VALUE;

            return false;
        }
    }

    const uint8_t fwd_before = fwd_live();
    evalue v = 0;
    if (*p == '[' || *p == '(') {
        /* Both group, and they are the same code because they mean the same
         * thing. A parenthesis reaching here has already been decided not to
         * be indirection -- see the positional rule in parse_operand -- so
         * inside an expression there is nothing left for it to be. */
        const bool square = *p == '[';
        if (expr_depth >= EXPR_MAXDEPTH) {
            zz.err = ZAP_E_EXPRESSION_NESTED_TOO_DEEPLY;

            return false;
        }
        expr_depth++;
        p++;
        uint8_t inner = 0;
        if (!expr_value(&v, &p, e, &inner)) {
            return false;
        }
        expr_depth--;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p != (square ? ']' : ')')) {
            zz.err = square ? ZAP_E_EXPECTEDX : ZAP_E_EXPECTED;

            return false;
        }
        p++;
    } else if (*p == '\'') {
        /* A character literal is its byte: one character and the closing
         * quote, or a backslash escape and the closing quote.
         *
         * The escapes are the string ones, which is one table for both and is
         * what the reference has -- `'\n'` is 0x0A there and so is `"\n"`.
         * They were missing here, so `ld a, '\a'` was "expected a character"
         * against the reference's 3E 07, and `'\''` -- the apostrophe, which
         * has no other spelling -- could not be written at all. */
        if (p[1] == '\\' && !(p[2] == '\'' && p[3] != '\'')) {
            /* An escape, unless the backslash *is* the character. `'\'` is the
             * backslash itself in the reference -- its own corpus writes
             * `'[' '\' ']'` for 5B 5C 5D -- and `'\''` is the apostrophe. The
             * two start alike and only the fourth character tells them apart:
             * a closing quote after the escaped quote means the apostrophe was
             * meant, and anything else means the literal ended at the quote
             * and the backslash was its content. */
            const int esc = str_escape(p[2]);
            if (esc < 0 || p[3] != '\'') {
                zz.err = ZAP_E_EXPECTED_CHARACTER;

                return false;
            }
            v = esc;
            p += 4;
        } else if (p[1] == '\\') {
            v = 0x5C;
            p += 3;
        } else {
            if (p[1] == '\n' || p[1] == 0 || p[2] != '\'') {
                zz.err = ZAP_E_EXPECTED_CHARACTER;

                return false;
            }
            v = (uint8_t) p[1];
            p += 3;
        }
    } else {
        const char* const ts = p;
        while (p < e && num_ch(*p)) {
            p++;
        }
        const int n = (int) (p - ts);
        if (n == 0) {
            zz.err = ZAP_E_EXPECTED_VALUE;

            return false;
        }
        if (n == 1 && *ts == '$') {
            /* The address of the instruction being assembled. `$` on its own;
             * with hex digits after it, it is the radix prefix instead, and
             * the scan above has already taken them. */
            v = zz.org + (int) (zz.o - zz.out);
        } else if (!expr_atom(&v, ts, n)) {
            return false;
        }
    }

    const uint8_t mine = (uint8_t) (fwd_live() & ~fwd_before);
    *fwdmask = mine;
    if (mine != 0) {
        if (invert) {
            /* `~later`: the fixup adds an address, it cannot complement one.
             * Refused rather than emitted with the operator quietly dropped. */
            expr_fwd_bad = true;
        } else if (sign < 0) {
            fwd_negate(mine);
        }
    }
    if (invert) {
        v = ~v;
    }
    *out = sign < 0 ? -v : v;
    *pp = p;

    return true;
}

/* Operator, right-hand side, fold into the total; again until what follows
 * binds less tightly than this level.
 *
 * A precedence climb, and in `-ez80` mode every operator has the same binding
 * power -- which makes the recursive call below return after a single term and
 * turns the climb into the reference's left-to-right fold. One algorithm, two
 * tables; the compatible answer is not a second code path to keep in step. */
static bool expr_climb(evalue* total, const char** pp, const char* e,
                       uint8_t minprec, int depth, uint8_t* fwdmask) {
    const char* p = *pp;
    if (depth > EXPR_MAXDEPTH) {
        zz.err = ZAP_E_EXPRESSION_NESTED_TOO_DEEPLY;

        return false;
    }
    for (;;) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        const char c = *p;
        if (exop[(uint8_t) c] == 0) {
            break;
        }
        const uint8_t prec = exprec[(uint8_t) c];
        if (prec < minprec) {
            break;
        }
        p++;
        if (c == '<' || c == '>') {
            /* Doubled or nothing: the reference refuses a single one rather
             * than reading it as a comparison, so `1<4` is an error. */
            if (*p != c) {
                zz.err = ZAP_E_EXPECTED_OR;

                return false;
            }
            p++;
        }

        /* Left associative, so the right-hand side takes only operators that
         * bind *more* tightly than this one. */
        /* Which slots *this* operator's right-hand side carries, which is not
         * the same as which the expression holds anywhere. In `later + 2*3`
         * the `*` joins two constants and is perfectly fine; asking only "is
         * there a forward reference about" refused it. */
        uint8_t rhs_mask = 0;
        evalue t = 0;
        if (!expr_term(&t, &p, e, &rhs_mask)) {
            return false;
        }
        if (!expr_climb(&t, &p, e, (uint8_t) (prec + 1), depth + 1,
                        &rhs_mask)) {
            return false;
        }
        if ((*fwdmask | rhs_mask) != 0) {
            /* A forward reference survives `+` and `-`, which move it between
             * added and subtracted and nothing more. Every other operator
             * would multiply, mask or shift an address that is not known yet,
             * and a symbol with a sign cannot say that. */
            if (c == '+') {
                *fwdmask |= rhs_mask;
            } else if (c == '-') {
                fwd_negate(rhs_mask);
                *fwdmask |= rhs_mask;
            } else {
                expr_fwd_bad = true;
            }
        }
        switch (c) {
            case '+': *total += t; break;
            case '-': *total -= t; break;
            case '*': *total *= t; break;
            case '/':
                /* The reference divides without looking, which on the Agon is
                 * whatever the runtime does with it. Refusing is the one place
                 * this deliberately does not match: there is no byte sequence
                 * to agree with. */
                if (t == 0) {
                    zz.err = ZAP_E_DIVISION_BY_ZERO;

                    return false;
                }
                *total /= t;
                break;
            case '<': *total <<= t; break;
            case '>': *total >>= t; break;
            case '&': *total &= t; break;
            case '|': *total |= t; break;
            default:  *total ^= t; break;
        }
    }
    *pp = p;

    return true;
}

/* A whole expression: a term, then everything that binds to it. */
static bool expr_value(evalue* out, const char** pp, const char* e,
                       uint8_t* fwdmask) {
    evalue total = 0;
    if (!expr_term(&total, pp, e, fwdmask)) {
        return false;
    }
    if (!expr_climb(&total, pp, e, 1, 0, fwdmask)) {
        return false;
    }
    *out = total;

    return true;
}

/* Every scan below is bounded, and the reader also keeps a newline one byte
 * past the last valid one.
 *
 * The sentinel is what makes the scans *terminate*: none of the character
 * classes contains a newline, so every loop stops on it whether or not it
 * tests the end. The bound is for something else entirely -- see EVERY SCAN IS
 * BOUNDED at the top of this file -- and it is the only defence against a loop
 * the compiler has rotated, which the sentinel cannot help with because a
 * rotated loop does not run off the end; it skips the first character.
 *
 * The single tests -- `*p == ','` and the like -- have no bound and need none.
 * They are not loops, so there is nothing to rotate, and the sentinel is what
 * makes reading one character past the content safe.
 *
 * The bounds here cost 0.06s of isa_real's 5.64, all of the price this rule
 * carries anywhere in the file. Nothing else measured at all. */
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

__attribute__((always_inline)) static inline bool parse_operand(dop* op, const char** pp, const char* e) {
    *op = dop_none;

    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }

    /* One class load decides what this operand is.
     *
     * It used to be four compares and then a load: three to ask whether the
     * operand list had ended, one for the open paren, and only then a class
     * lookup to ask whether a register starts here. All of that is one byte's
     * worth of information about one character, so it is now read once.
     *
     * The end of the operand list is still checked here rather than by
     * bounding the scan at the end of the line, because finding that end meant
     * a whole extra pass over the source. */
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
                /* This is the loop the whole rule came from, and the only
                 * one whose failure was ever *seen* rather than reasoned
                 * about: unbounded, `ld a, 0x42` parsed as 0x4 and left `2`
                 * behind, because the rotated loop never examined the first
                 * character and stopped one short.
                 *
                 * It does not reduce -- the same loop in isolation compiles
                 * correctly -- and rewriting it to index from a base rather
                 * than advance a pointer does not help; that was tried and
                 * fails the same way. Full diagnosis on the `sentinel`
                 * branch. Every scan in the file now carries the same bound
                 * for the same reason; see the top of this file. */
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
                    /* Not a plain decimal, so the general parser has it.
                     *
                     * The fast path used to *refuse* what it could not finish
                     * rather than hand it over, which made `(ix + 05h)` and
                     * `(ix + 0x05)` "bad displacement" -- both begin with a
                     * digit, so the decimal scan claimed them and then stopped
                     * on the `h` or the `x`. `$05` worked, because a dollar is
                     * not a digit and it never reached here. That is a real
                     * program's spelling: it is how the Agon corpus writes
                     * `ld a, (ix + 05h)`. */
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
                        zz.err = ZAP_E_LABEL_DEFINED_ALREADY;

                        return false;
                    }
                }
                op->disp = neg ? -d : d;
                if (op->disp < -128 || op->disp > 127) {
                    /* "Index register offset exceeded" there. One signed byte
                     * is what the instruction has room for, so anything else
                     * would be emitted truncated and silently wrong. */
                    zz.err = ZAP_E_INDEX_OFFSET_OUT_RANGE;

                    return false;
                }
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
            }

            if ((op->mode & INDIRECT) != 0) {
                if (*p != ')') {
                    zz.err = ZAP_E_EXPECTED;

                    return false;
                }
                p++;
                /* Past what follows the parenthesis, which the general path
                 * below already does after its own `)`.
                 *
                 * It is here rather than in assemble_line because it is
                 * needed in one case and one only: finding the comma before a
                 * third operand, in `res 5, (ix+1) , h`, which the reference
                 * takes and gives the same bytes as the unspaced form.
                 *
                 * Three placements, measured:
                 *
                 *   in assemble_line, after the second operand   +0.04 real
                 *   here, on the indirect register operands      +0.02 real
                 *   nowhere, and the spaced form refused          0.00
                 *
                 * On isa_degenerate, which is nothing but instructions and
                 * has the most indirect operands of the set, this placement
                 * is 0.04 rather than 0.02. It is kept: `res 5, (ix+1) , h`
                 * assembling to the same bytes as `res 5,(ix+1),h` is the
                 * whole claim this project makes, and a spelling nobody
                 * writes is exactly the kind of thing that turns out to be in
                 * somebody's file. */
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
         * num_parse has always known the suffix forms; the operand simply
         * never reached it, and this said "unknown operand" instead. Forty
         * forms of the reference's own corpus were wrong for as long as that
         * was true, and the corpus could not say so because it was filtered
         * through zap.
         *
         * Rewinding is all it takes: num_ch admits letters, so the literal
         * scan below reads the whole token, and the closing paren of an
         * indirect operand is handled there too.
         *
         * What it does not take is scanning the token again. C_NUM contains
         * every character C_NAME does and three more -- $, # and % -- so the
         * literal scan below re-reads exactly the characters just read, and
         * then stops in the same place, unless the character that ended the
         * name run is one of those three. That is one class test to find out,
         * against a second pass over the whole token, and the token here is a
         * label: labels are most of what reaches this line and they are the
         * longest thing an operand can be.
         *
         * The apostrophe needs no special case. It is not a C_NUM character
         * either, so the literal scan stopped in front of it too; handing over
         * the end of the name run rather than the position after the
         * apostrophe is what the rewind was already doing. */
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
            /* Bounded, like every scan here, and this one was written that
             * way from the start for the reason at the displacement scan. */
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
            v = zz.org + (int) (zz.o - zz.out);
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
                if (!zz.anon_has_prev) {
                    zz.err = ZAP_E_NO_ANONYMOUS_LABEL_ABOVE_ONE;

                    return false;
                }
                v = zz.anon_prev;
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
             * Tried after the literal forms and not before them, because a
             * hexadecimal constant written with a trailing h begins with a
             * letter too -- `aabbcch` is a number and `aabbcc` is a name, and
             * only the suffix tells them apart.
             *
             * And decided by whether any radix accepts it, not by what it
             * starts with. `$42`, `#42` and `%1010` are literals that begin
             * with neither a letter nor a digit; `2b`, `1z` and `123abc` are
             * labels that begin with a digit, and the reference lets them be
             * referenced as well as defined. Asking "does it start with a
             * letter" got the first three right and the last three wrong.
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
                zz.err = ZAP_E_EXPECTED_VALUE;

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
                zz.err = ZAP_E_EXPECTED;

                return false;
            }
            p++;

            /* The positional rule, and the whole of what makes `(` mean two
             * things.
             *
             * A parenthesis that opens the operand and whose match closes it
             * is indirection -- `ld a, (var)`. One whose match does *not*
             * close it was grouping all along, and this is where that becomes
             * knowable: `add a, (RTABLE-DTABLE)/2` is real corpus code, and up
             * to the `/` it is indistinguishable from `ld a, (var)`.
             *
             * So the operand is parsed as indirection first and reinterpreted
             * here, rather than scanned ahead to its matching parenthesis
             * before its shape is known. The scan would cost every `(hl)` and
             * `(ix+d)` in the file; this costs one table lookup on the operands
             * that reached the general parse with a parenthesis on the front,
             * which the register path has already taken the common ones out of.
             *
             * Re-reading the operand from the start is what the rewind is:
             * `*pp` still holds where it began, because nothing writes it
             * until the end. */
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

/* Defined with the directives below, and used by both. */
__attribute__((always_inline))
static inline const char* lit_value(const char* p, const char* e, evalue* out);

/* ----------------------------------------------------------------- equ */

/* `EQU`, or `.EQU`, in any case -- tested in place, and advancing past it if it
 * is one.
 *
 * Four compares and no token scan, because a scan needs somewhere to keep the
 * end of it and this function runs inside assemble_line, where a local costs
 * three bytes of frame and the frame costs `iy`. The same test written with a
 * `const char* q` measured a whole 1.8% worse across every benchmark.
 *
 * Reading ahead is safe without a bound, and this is not an exception to the
 * rule at the top of the file: there is no loop here to rotate. The chain
 * short-circuits on the first mismatch, and the buffer ends in a newline that
 * matches nothing here, so it can reach the sentinel and never pass it.
 * The class test on the fourth character is what keeps `equx` from being one.
 *
 * The reference takes `EQU`, `equ` and `.EQU`; it does not take `X EQU 5`
 * without the colon -- that is "Invalid mnemonic 'X'" there -- and it has no
 * `=`. Each of those was measured against it rather than assumed. */
static bool is_equ_at(const char* p) {
    if (*p == '.') {
        p++;
    }

    return (p[0] | 0x20) == 'e' && (p[1] | 0x20) == 'q' && (p[2] | 0x20) == 'u'
           && (cclass[(uint8_t) p[3]] & C_MNEM) == 0;
}

/* The rest of `name: EQU value`.
 *
 * `named` is the symbol the label path just defined, holding the address the
 * line happened to be at; this replaces it with the value. Defining it first
 * and overwriting is what keeps the ordinary label path unchanged -- and the
 * redefinition check, which has to happen either way, happens there.
 *
 * The value must be knowable now. `X: EQU Y+1` with `Y` still ahead is
 * "Unknown identifier" in the reference too, which has a second pass and could
 * have resolved it: a forward reference in an EQU is refused by both, so this
 * is one of the places where one pass costs nothing.
 *
 * A forward reference *to* an EQU is a different thing and already works.
 * `ld a, X` above `X: EQU 5` goes through the fixup list like any other label
 * that is not defined yet, and patching reads the value the same way it reads
 * an address. Nothing here had to know about it. */
__attribute__((noinline))
static bool equ_line(const char* name, int nlen, const char* p,
                     const char* e, const char** stop) {
    /* Steps over the EQU itself. is_equ_at only answered whether one is here,
     * and telling it to also report where it ended meant taking the address of
     * assemble_line's line pointer -- which is the one thing that function
     * cannot afford. Three characters re-read once per EQU against a pointer
     * spilled on every line. */
    if (*p == '.') {
        p++;
    }
    p += 3;
    while (p < e && is_space_ch(*p)) {
        p++;
    }

    /* The label path has already defined this name at the address the line
     * happened to be at, including the check for a second definition and the
     * scope it ends -- a name given a value ends one exactly as a label does,
     * which is what the reference does too. All that is left is to replace the
     * address with the value.
     *
     * Found by name rather than handed over, for the reason given at the call
     * site. Interning a name that exists is a lookup and no allocation. */
    sym* named;
    if (*name == '@') {
        if (nlen == 2 && name[1] == '@') {
            /* An anonymous label has no name to attach a value to, and the
             * reference refuses this too. */
            zz.err = ZAP_E_INVALID_LABEL;

            return false;
        }
        named = loc_intern(name, nlen);
    } else {
        named = sym_intern(name, nlen);
    }
    if (named == NULL) {
        return false;
    }

    /* Undefined while its own value is being worked out, so that `X: EQU X+1`
     * is refused rather than quietly reading the address the label path just
     * gave it. The reference refuses it too. Two stores, on the EQU path
     * only. */
    named->defined = false;

    evalue value = 0;
    const char* const lit = lit_value(p, e, &value);
    if (lit != NULL) {
        p = lit;
    } else {
        fwd_reset(NULL);
        uint8_t fwdmask = 0;
        if (!expr_value(&value, &p, e, &fwdmask)) {
            return false;
        }
        if (expr_fwd != NULL) {
            zz.err = ZAP_E_LABEL_DEFINED_ALREADY;

            return false;
        }
    }
    named->defined = true;
    named->addr = value;

    while (p < e && is_space_ch(*p)) {
        p++;
    }
    *stop = p;

    /* No test for what follows. The line loop already reports anything left
     * over -- `X: EQU 5 6` is "unexpected text after the instruction" there --
     * and a second check here was dead code that only looked like safety. */

    return true;
}

/* Another buffer of whole lines, or not.
 *
 * Out of line, and the `bool` it needs is the reason. The reader reports an
 * over-long line through an out-parameter, and holding that across the call
 * inside `run` took its frame from 13 bytes to 16 -- in the function that
 * holds the line loop. Here it costs nothing, because this is entered once per
 * 16 KB of source. A failure is told from an end of file by `zz.err`, which
 * exists either way.
 *
 * The check itself is new. The reader has always said when one line does not
 * fit its buffer and the loop has always ignored it, ending the assembly
 * *successfully* and writing whatever came before -- unreachable until a
 * directive could put 16 KB on one line, and wrong the whole time regardless. */
__attribute__((noinline))
static bool line_fill(buf_reader* r) {
    bool too_long = false;
    if (br_fill_lines(r, &too_long)) {
        return true;
    }
    if (too_long) {
        /* zz.line counts the lines already assembled, so this names the one
         * before the offending line. That is still where to start looking. */
        zz.line++;
        zz.err = ZAP_E_LINE_TOO_LONG;
    }

    return false;
}


static bool run_lines(void);

/* The local-label scope a macro expansion runs in, set aside and put back.
 *
 * An expansion needs a scope of its own -- the reference gives it one, so a
 * body defining `@loc` may be invoked twice, and a body naming `@a` does not
 * see the caller's -- and the caller's has to survive, because the reference
 * lets a local defined before an invocation be named after it.
 *
 * The scope is entered by advancing the generation stamp, which is what ends
 * an ordinary scope: every bucket then belongs to an older generation and
 * reads as empty. Leaving it is the stamp going back.
 *
 * What the stamp cannot undo is a bucket the body *wrote*, because claiming a
 * bucket overwrites the head the caller had. Those are written down as they
 * happen -- see undo_note -- and there are only ever as many as the body has
 * distinct local names.
 *
 * The first version copied all 64 buckets out and back instead. It was correct
 * and it cost 12,300 cycles an invocation against 770, because an indexed
 * struct assignment in a loop is not a block move on this machine and memcpy
 * of 256 bytes is not free either. isa_real spent 1.48 seconds in it.
 *
 * The slot and name arenas are shared with the caller rather than replaced.
 * The hazard there was `scope_end` rewinding them to their first block on top
 * of the caller's live slots, and it cannot arise: a scope only ends on a
 * global label or an EQU, and the reference refuses both inside a macro body,
 * which zap now does too. So the body appends, and the counters rewind at the
 * end to hand the space back. */
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

__attribute__((noinline))
static bool scope_push(locsave* sv) {
    sv->loccur = zz.loccur;
    sv->locnames = zz.locnames;
    sv->locs_used = zz.locs_used;
    sv->locnames_used = zz.locnames_used;
    sv->lfix_used = zz.lfix_used;
    sv->subfix_used = zz.subfix_used;
    sv->undo_used = zz.undo_used;
    sv->gen = zz.gen;
    /* The scope end a global label left pending belongs to the caller. Left
     * standing, the first local in the body would notice it -- the body counts
     * its own line numbers, so the comparison that defers it never matches --
     * and end the expansion's scope in the caller's name, clearing the flag.
     * The caller's next local then would not open a scope at all, and the one
     * above it would still hold whatever the previous scope defined. */
    sv->scope_line = zz.scope_line;
    zz.scope_line = 0;

    if (++zz.gen == 0) {
        /* The stamp has wrapped, so a bucket left over from 256 scopes ago
         * would read as belonging to this one. The undo log is what puts them
         * back, and it records what it finds, so emptying them here is safe. */
        for (int b = 0; b < NLOCB; b++) {
            zz.locs[b].gen = 0;
            zz.locs[b].head = NULL;
        }
        zz.gen = 1;
    }

    return true;
}

/* Settles what the expansion referred to and puts the caller's scope back.
 *
 * Only the fixups the expansion added are resolved -- the caller's are still
 * outstanding and belong to a scope that has not ended. */
__attribute__((noinline))
static bool scope_pop(locsave* sv) {
    /* The body's locals go the same way a scope's do, so a fixup that names a
     * global and one of them is settled in halves here too -- and only the
     * ones this expansion added: the caller's are still outstanding and its
     * locals are not defined yet. */
    bool ok = fold_subs(sv->subfix_used);
    for (int i = sv->lfix_used; ok && i < zz.lfix_used; i++) {
        if (!patch_fixup(&zz.lfixups[i])) {
            ok = false;
            break;
        }
    }

    while (zz.undo_used > sv->undo_used) {
        const locundo* u = &zz.undo[--zz.undo_used];
        zz.locs[u->b].head = u->head;
        zz.locs[u->b].gen = u->gen;
    }
    zz.gen = sv->gen;
    zz.scope_line = sv->scope_line;
    zz.lfix_used = sv->lfix_used;

    /* The arena counters go back so the space is handed out again. The name
     * blocks only rewind if the body did not need a new one: nam_take pushes a
     * new block in front of the list, and rewinding past it would lose it. */
    zz.loccur = sv->loccur;
    zz.locs_used = sv->locs_used;
    if (zz.locnames == sv->locnames) {
        zz.locnames_used = sv->locnames_used;
    }

    return ok;
}

/* Which directive a token is, or none.
 *
 * Reached only when mnemonic_of has already failed, so an ordinary instruction
 * line never runs a character of it. That is why it is spelled out as compares
 * rather than bucketed the way mnemonics are: on the path it is on, twelve
 * names and a switch on length cost nothing worth measuring, and the table it
 * would otherwise need would be paid for in memory by every program.
 *
 * The reference takes a leading dot on all of them -- `.DB`, `.ALIGN` -- and
 * every spelling below was checked against it. `WORD`, `DWORD`, `DEFL`, `DC`,
 * `TEXT` and `DB8` are *not* directives there, however plausible they look. */
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
 * Not the same directive as DS, which is why they are not the same number.
 * `blkb` was an alias for DS here and it is wrong twice: DS reserves 0xFF and
 * ignores a fill argument, and a run of it at the end of a file is dropped;
 * BLKB writes the fill it is given and is never dropped. `blkb 1, 1` came out
 * as 0xFF and `blkb 1, 255` at the end of a file came out as nothing.
 *
 * BLKL was absent for as long as the evaluator was the machine's word. It is
 * four bytes and the corpus fills it with `0x55555555` and `-2147483648`, and
 * a BLKL that quietly wrote the low three bytes and a sign would have been
 * wrong for half of the reference's own cases. See evalue for what changed. */
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
/* MACRO and ENDMACRO below the conditionals, and that ordering is load-bearing:
 * a line inside a switched-off branch asks `kind >= DIR_IF` and nothing else,
 * so anything at or above IF is handled while skipping and anything below is
 * skipped. With MACRO above them, `IF 0 / MACRO m / ... / ENDMACRO / ENDIF`
 * captured the body and defined the macro -- the reference skips it, and
 * `IF 1 / macro m / db 0 / endmacro / ELSE / macro m / db 1 / endmacro / ENDIF`
 * assembled the branch that was not taken. */
#define DIR_MACRO    20
#define DIR_ENDMACRO 21
#define DIR_IF       22
#define DIR_ELSE     23
#define DIR_ENDIF    24

/* ---------------------------------------------------------------- macros */

/* Both defined with the directives below. A macro parameter may not be a
 * mnemonic or a directive, and that is asked where the parameters are read. */
static uint8_t directive_of(const char* s, int n);
static bool is_equ_at(const char* p);


/* Macro bodies grow a line at a time and are not in the name blocks, because a
 * block is fixed and a body is not known until ENDMACRO. One allocation per
 * macro, doubled when it fills, freed with everything else. */
static bool mark_room(macro* m) {
    if (m->nmarks < m->markcap) {
        return true;
    }
    const int want = m->markcap == 0 ? 8 : m->markcap + m->markcap;
    Z_SITE("macro bodies");
    macmark* grown = (macmark*) realloc(m->marks, (size_t) want * sizeof(macmark));
    if (grown == NULL) {
        return false;
    }
    m->marks = grown;
    m->markcap = want;

    return true;
}

static bool macro_room(macro* m, int need) {
    if (m->bodylen + need <= m->bodycap) {
        return true;
    }
    int want = m->bodycap < 256 ? 256 : m->bodycap + m->bodycap;
    while (m->bodylen + need > want) {
        want += want;
    }
        Z_SITE("macro bodies");
    char* grown = (char*) realloc(m->body, (size_t) want);
    if (grown == NULL) {
        return false;
    }
    m->body = grown;
    m->bodycap = want;

    return true;
}

static const macro* macro_at(const char* s, int n) {
    const char c0 = (char) (*s | 0x20);
    macro* prev = NULL;
    for (macro* m = zz.macros; m != NULL; prev = m, m = m->next) {
        /* Length and first character before the call, for the same reason the
         * substitution loop asks them: the list is walked once per invocation
         * and most of it is not this macro. */
        if (m->namelen != (uint8_t) n || (m->name[0] | 0x20) != c0) {
            continue;
        }
        if (!same_ci_full(m->name, s, n)) {
            continue;
        }

        /* Found, and moved to the front.
         *
         * A definition is *prepended*, so the list is in reverse order of
         * definition -- and a program defines its macros in a header and then
         * uses them for the rest of the file, so the ones it uses most were
         * the deepest in the list. Measured on the Agon with forty
         * definitions: invoking the one at the head costs nothing over having
         * a single macro, and invoking the one at the tail costs **5,530
         * cycles**, or 138 a link.
         *
         * Order carries no meaning here -- a second definition of a name is
         * refused where it is written, so there is never more than one match
         * -- which is what makes this safe as well as cheap. */
        if (prev != NULL) {
            prev->next = m->next;
            m->next = zz.macros;
            zz.macros = m;
        }

        return m;
    }

    return NULL;
}

/* `MACRO name [param, param, ...]`.
 *
 * The name and the parameters are copied into the name blocks, which never
 * move; the parameters go in as a length byte followed by the text, so walking
 * them needs no second array and no terminator. */
__attribute__((noinline))
static bool macro_begin(const char** pp, const char* e) {
    if (zz.defining != NULL) {
        /* "No macro definitions allowed inside a macro" there. */
        zz.err = ZAP_E_MACROS_DO_NOT_NEST;

        return false;
    }
    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    const char* ns = p;
    while (p < e && name_ch(*p)) {
        p++;
    }
    const int nn = (int) (p - ns);
    if (nn == 0 || nn > 255) {
        zz.err = ZAP_E_EXPECTED_MACRO_NAME;

        return false;
    }

    if (macro_at(ns, nn) != NULL) {
        /* "Macro already defined" there, and case-blind, as the lookup is. */
        err_tok(ns, nn);
        zz.err = ZAP_E_MACRO_ALREADY_DEFINED;

        return false;
    }

    Z_SITE("macro table");
    macro* m = (macro*) calloc(1, sizeof(macro));
    if (m == NULL) {
        zz.err = ZAP_E_OUT_MEMORY_MACROS;

        return false;
    }
    /* One byte more than the name, for a terminator. Nothing else in the name
     * blocks carries one -- a label is a pointer and a length -- but an
     * expansion puts this name in `zz.path`, where a failure inside the body
     * reports it, and that is read as a string. Without the nul the message
     * came out as the whole arena from the name onwards. */
    char* nm = nam_take(&zz.names, &zz.names_used, nn + 1);
    if (nm == NULL) {
        free(m);
        zz.err = ZAP_E_OUT_MEMORY_MACROS;

        return false;
    }
    for (int i = 0; i < nn; i++) {
        nm[i] = ns[i];
    }
    nm[nn] = 0;
    m->name = nm;
    m->namelen = (uint8_t) nn;

    /* The parameters, each stored as a length and then its characters. */
    for (;;) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (!name_ch(*p)) {
            break;
        }
        const char* ps = p;
        while (p < e && name_ch(*p)) {
            p++;
        }
        const int pn = (int) (p - ps);
        if (pn > 255) {
            /* Freed on the way out. The macro is not linked into the list
             * until the body has been captured, so nothing else will ever see
             * it -- which also means nothing else will ever free it. This path
             * leaked before ASan was pointed at it. */
            free(m);
            zz.err = ZAP_E_MACRO_PARAMETER_NAME_TOO_LONG;

            return false;
        }
        /* "Invalid argument name" there, for anything the body could not tell
         * from what it stands for: a number in any radix, and any mnemonic or
         * directive. Register names are allowed -- `macro m hl` assembles --
         * because a parameter is substituted by whole identifier and a
         * register is one the body could have meant either way.
         *
         * Asked once per parameter at the definition, which is a dozen times
         * in a file rather than once per expansion. */
        if (numeric_token(ps, pn) || mnemonic_of(ps, pn) != NULL
            || directive_of(ps, pn) != DIR_NONE || is_equ_at(ps)) {
            free(m);
            zz.err = ZAP_E_MACRO_PARAMETER_NOT_NUMBER_OR_MNEM;

            return false;
        }
        char* at = nam_take(&zz.names, &zz.names_used, pn + 1);
        if (at == NULL) {
            free(m);
            zz.err = ZAP_E_OUT_MEMORY_MACROS;

            return false;
        }
        if (m->params == NULL) {
            m->params = at;
        }
        at[0] = (char) pn;
        for (int i = 0; i < pn; i++) {
            at[1 + i] = ps[i];
        }
        m->nparam++;
    }

    m->defline = zz.line;
    if (zz.path != NULL) {
        int pn = 0;
        while (zz.path[pn] != 0) {
            pn++;
        }
        char* dp = nam_take(&zz.names, &zz.names_used, pn + 1);
        if (dp == NULL) {
            free(m);
            zz.err = ZAP_E_OUT_MEMORY_MACROS;

            return false;
        }
        for (int i = 0; i < pn; i++) {
            dp[i] = zz.path[i];
        }
        dp[pn] = 0;
        m->defpath = dp;
    }

    m->next = zz.macros;
    zz.macros = m;
    zz.defining = m;
    zz.line_mode = LINE_CAPTURE;
    *pp = p;

    return true;
}

/* One line of a macro body, copied in as it stands. Nothing on it is parsed
 * until the macro is invoked, which is why a body may hold names that do not
 * exist yet and arguments that are not values. */
/* Every parameter in the span just copied, recorded where it is.
 *
 * Once per line of the definition, which a file does a dozen times, against
 * once per identifier per invocation, which isa_real does four hundred times
 * over. The scan is the one that used to run at expansion: any name character
 * starts a token, digits included -- a parameter cannot be a number, which is
 * checked where they are read, but it can begin with a digit, and the
 * reference's own corpus has `macro test 0123456789abcdef...`.
 *
 * Case-sensitively, which is what the reference does: `MACRO m v` with `V` in
 * the body is "Unknown identifier" there. Everything else about a macro is
 * case-blind, so this had been assumed rather than measured, once. */
static bool macro_marks(macro* m, int from, int to) {
    int b = from;
    while (b < to) {
        if (!name_ch(m->body[b])) {
            b++;
            continue;
        }
        int j = b;
        while (j < to && name_ch(m->body[j])) {
            j++;
        }
        const int take = j - b;
        const char* pp = m->params;
        for (int k = 0; k < m->nparam; k++) {
            const int pn = (uint8_t) pp[0];
            /* The length and then the first character before the call, as the
             * expansion loop used to ask them. */
            if (pn == take && pp[1] == m->body[b]
                && same_full(pp + 1, &m->body[b], take)) {
                if (!mark_room(m)) {
                    return false;
                }
                m->marks[m->nmarks].off = b;
                m->marks[m->nmarks].k = (uint8_t) k;
                m->marks[m->nmarks].len = (uint8_t) take;
                m->nmarks++;
                break;
            }
            pp += pn + 1;
        }
        b = j;
    }

    return true;
}

static bool macro_line(const char* p, const char* e) {
    const char* q = p;
    while (q < e && *q != '\n') {
        q++;
    }
    const int n = (int) (q - p) + 1;
    if (!macro_room(zz.defining, n)) {
        zz.err = ZAP_E_OUT_MEMORY_MACROS;

        return false;
    }
    const int at = zz.defining->bodylen;
    for (int i = 0; i < n - 1; i++) {
        zz.defining->body[at + i] = p[i];
    }
    zz.defining->body[at + n - 1] = '\n';
    zz.defining->bodylen = at + n;

    /* Only where a parameter could be found: a macro with none has nothing to
     * mark and pays nothing for the feature. */
    if (zz.defining->nparam != 0 && !macro_marks(zz.defining, at, at + n - 1)) {
        zz.err = ZAP_E_OUT_MEMORY_MACROS;

        return false;
    }

    /* Every spelling of a local starts with one: `@name`, `@@`, `@f`, `@b`.
     * So one character decides whether the expansion needs a scope. */
    if (!zz.defining->haslocal) {
        for (int i = at; i < at + n - 1; i++) {
            if (zz.defining->body[i] == '@') {
                zz.defining->haslocal = true;
                break;
            }
        }
    }

    return true;
}

/* One expansion of a macro: the body with the arguments put in, assembled.
 *
 * Substitution is textual and by whole identifier, which is measured and not
 * assumed. With `x` bound to `1+1`, the reference assembles `db 10-x` as ten --
 * `10-1+1` read left to right -- and not as eight, and `db 2*x` as three and
 * not four. It also leaves `xy` alone when the parameter is `x`, so it is
 * whole names and not raw text.
 *
 * The expanded text is read the way an included file is read: a reader over
 * memory, the parent one set aside, and the same `run_lines` re-entered. The
 * comment on `br_open_mem` has said macro expansion needs it since before there
 * were macros.
 *
 * Each expansion is its own scope for local labels, because the reference makes
 * it one: a macro whose body defines `@a` may be invoked twice without a
 * redefinition, and `@a` cannot be named after the expansion ends.
 */
__attribute__((noinline))
/* Defined with the line loop, and called from the macro expansion below --
 * which assembles the body itself rather than handing it to a reader. */
static bool assemble_line(const char* p, const char* e, const char** stop);

/* The invocation's arguments, as spans of the line that carried them.
 *
 * Kept in `dz` rather than in a frame. Eight pointers and eight lengths is 48
 * bytes on the Agon, and while they sat in the same frame as the scope save
 * and the body loop everything in there was past the 128 bytes an `ix`
 * displacement reaches -- thirty accesses through a five-instruction address
 * computation apiece. Indexed by depth times MACRO_MAXPARAM, which is eight
 * and therefore a shift rather than a call to __imulu.
 *
 * A macro takes as many arguments as it declares and the reference counts
 * them -- "0 provided, 1 expected". */
__attribute__((noinline))
static bool macro_args(const macro* m, const char* p, const char* e,
                       const char** stop, int base) {
    int nargs = 0;
    for (;;) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (p >= e || *p == '\n' || *p == ';') {
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (nargs == MACRO_MAXPARAM) {
            zz.err = ZAP_E_TOO_MANY_MACRO_ARGUMENTS;

            return false;
        }
        /* The end is carried forward rather than walked back from.
         *
         * `while (ae > as && is_space_ch(ae[-1])) ae--;` is the obvious way to
         * write this and the compiler gets it wrong at -Oz: it commits the
         * decrement before the test and then reads the byte off the pointer it
         * has already moved, so the character examined is `ae[-2]`. A single
         * character argument therefore saw the space in front of it, trimmed
         * itself away, and expanded to nothing -- `mload 5` became `ld a,` and
         * "no such instruction form", while `mload 65` was fine because the
         * byte one further back was still part of the argument.
         *
         * Nothing on the host reproduces it; it took a benchmark that invokes
         * macros to find, and reading the generated code to explain. Tracking
         * the last non-space while scanning needs no backward index at all,
         * and is one pass rather than two. */
        const char* as = p;
        const char* ae = p;
        while (p < e && *p != '\n' && *p != ';' && *p != ',') {
            if (!is_space_ch(*p)) {
                ae = p + 1;
            }
            p++;
        }
        zz.margp[base + nargs] = as;
        zz.margn[base + nargs] = (int) (ae - as);
        nargs++;
    }
    *stop = p;
    if (nargs != m->nparam) {
        zz.err = ZAP_E_WRONG_NUMBER_MACRO_ARGUMENTS;

        return false;
    }

    return true;
}

/* One line of the body, with its parameters replaced, ready to assemble.
 *
 * The places a parameter occurs were found when the body was read, so there is
 * nothing to classify here: copy up to the next mark, copy the argument,
 * carry on. What this replaces asked every parameter about every identifier of
 * every line on every invocation, to rediscover something the body settles
 * once -- 1,940 cycles a parameter.
 *
 * `mi` is where the caller has got to in the mark list, which is in body order,
 * so the whole body is walked once across all its lines.
 *
 * Returns the length written, including the newline that ends it, or -1.
 * `bufp` and `capp` are the caller's, so the buffer is grown once and reused
 * for every line of the body and every later invocation at this depth. */
__attribute__((noinline))
static int macro_subst(const macro* m, int lo, int hi, int base,
                       const macmark** mkp, const macmark* mkend,
                       char** bufp, int* capp) {
    char* out = *bufp;
    int cap = *capp;
    int len = 0;
    int cur = lo;
    /* A local cursor, written back once. Through the caller's pointer it
     * would be a load and a store on every mark, which is the fault the
     * comment scan in the line loop carried for a long time. */
    const macmark* mk = *mkp;

    while (mk < mkend && mk->off < hi) {
        const int span = mk->off - cur;
        const int need = zz.margn[base + mk->k];
        /* Two spare: the newline this line is finished with, and room for the
         * tail copied after the loop to ask for its own. */
        if (len + span + need + 2 > cap) {
            cap = (len + span + need + 2) * 2;
            Z_SITE("macro expansion");
            char* grown = (char*) realloc(out, (size_t) cap);
            if (grown == NULL) {
                zz.err = ZAP_E_OUT_MEMORY_MACROS;

                return -1;
            }
            out = grown;
        }
        const char* b = m->body + cur;
        char* o = out + len;
        for (int i = 0; i < span; i++) {
            o[i] = b[i];
        }
        len += span;
        const char* const arg = zz.margp[base + mk->k];
        o = out + len;
        for (int i = 0; i < need; i++) {
            o[i] = arg[i];
        }
        len += need;
        cur = mk->off + mk->len;
        mk++;
    }
    *mkp = mk;

    const int span = hi - cur;
    if (len + span + 2 > cap) {
        cap = (len + span + 2) * 2;
        Z_SITE("macro expansion");
        char* grown = (char*) realloc(out, (size_t) cap);
        if (grown == NULL) {
            zz.err = ZAP_E_OUT_MEMORY_MACROS;

            return -1;
        }
        out = grown;
    }
    const char* b = m->body + cur;
    char* o = out + len;
    for (int i = 0; i < span; i++) {
        o[i] = b[i];
    }
    len += span;

    /* The newline the line has to end on: every scan in assemble_line stops
     * on one, and the trailing-text check in macro_expand reads it. */
    out[len++] = '\n';

    *bufp = out;
    *capp = cap;

    return len;
}

/* Assembles the body, a line at a time, in a scope of its own.
 *
 * There is no reader and no nested line loop. The body is already a run of
 * lines -- macro_line stored it that way -- so each one is substituted into a
 * buffer and handed straight to assemble_line, which is what the line loop
 * would have done with it after opening a reader over memory to find the
 * newlines that were never lost.
 *
 * What that removes, per invocation, is the 27-byte reader saved and put back,
 * br_use_mem, br_destroy, a nested run_lines and the br_fill_lines it calls
 * once to be told the buffer is spent. The measurements are in
 * .internal/performance-notes.md.
 *
 * The file the invocation came from is left open, as it was before: an
 * INCLUDE gives its parent's handle up because MOS has few of them, and an
 * expansion opens no file at all. */
__attribute__((noinline))
static bool macro_expand(const macro* m, const char* p, const char* e,
                         const char** stop) {
    if (zz.depth >= INCLUDE_MAXDEPTH) {
        zz.err = ZAP_E_MACROS_NESTED_TOO_DEEPLY;

        return false;
    }
    const int base = zz.depth * MACRO_MAXPARAM;
    if (!macro_args(m, p, e, stop, base)) {
        return false;
    }

    const char* const saved_path = zz.path;
    const int saved_line = zz.line;
    zz.path = m->name;
    zz.line = 0;
    zz.expanding++;

    char* buf = zz.expbuf[zz.depth];
    int cap = zz.expcap[zz.depth];
    const int slot = zz.depth;
    zz.depth++;

    /* A scope of its own for the body, with the caller's kept and put back --
     * and only for a body that has a local in it. See `haslocal`. */
    locsave sv;
    if (m->haslocal) {
        scope_push(&sv);
    }

    bool ok = true;
    /* Offsets into the body, not pointers, because the body may have been
     * realloc'd since the marks were taken and an offset does not care. The
     * mark cursor is a pointer, and the list is in body order, so it walks
     * once across every line. */
    const macmark* mk = m->marks;
    const macmark* const mkend = m->marks + m->nmarks;
    int b = 0;
    const int bend = m->bodylen;
    while (b < bend) {
        int le = b;
        while (le < bend && m->body[le] != '\n') {
            le++;
        }
        /* A body with no parameters is assembled where it lies.
         *
         * Nothing in it can be substituted, so the copy has nothing to do:
         * macro_line already stored the line with the newline that ends it,
         * and assemble_line reads a span rather than a buffer of its own.
         * Half the macros in a real header take no arguments -- a save, a
         * restore, a wait -- and they now cost no buffer, no copy and no
         * call. */
        const char* ls;
        const char* lend;
        if (m->nparam == 0) {
            ls = m->body + b;
            lend = m->body + le + 1;
        } else {
            const int len =
                macro_subst(m, b, le, base, &mk, mkend, &buf, &cap);
            if (len < 0) {
                ok = false;
                break;
            }
            ls = buf;
            lend = buf + len;
        }
        zz.line++;

        const char* st = ls;
        if (!assemble_line(ls, lend, &st)) {
            if (!zz.errhave) {
                err_line(zz.errline, ls, lend);
                zz.errhave = true;
                /* The body line, named as a line of the file it was written
                 * in rather than as an index into the body. */
                zz.line = m->defline + zz.line;
                zz.errfile = m->defpath;
                zz.errmacro = m->name;
                /* And the other end of it. This is the only place that holds
                 * both -- by the time the line loop sees the failure, the
                 * path and the line have been given to the macro. */
                zz.errfrompath = saved_path;
                zz.errfromline = saved_line;
            }
            ok = false;
            break;
        }
        /* Whatever ended the line, exactly as the line loop decides it. The
         * check is written out again here rather than shared: sharing means
         * run_lines calling into it, and that loop runs on every line of every
         * file. suffixed_insn duplicates assemble_line's tail for the same
         * reason. */
        if (*st != '\n') {
            const char* q = st;
            while (q < lend && is_space_ch(*q)) {
                q++;
            }
            if (*q == ';') {
                while (q < lend && *q != '\n') {
                    q++;
                }
            } else if (*q != '\n') {
                zz.err = ZAP_E_UNEXPECTED_TEXT_AFTER_INSTRUCTION;
                if (!zz.errhave) {
                    err_line(zz.errline, ls, lend);
                    zz.errhave = true;
                    zz.line = m->defline + zz.line;
                    zz.errfile = m->defpath;
                    zz.errmacro = m->name;
                    zz.errfrompath = saved_path;
                    zz.errfromline = saved_line;
                }
                ok = false;
                break;
            }
        }
        b = le + 1;
    }

    if (m->haslocal && !scope_pop(&sv)) {
        ok = false;
    }

    zz.expbuf[slot] = buf;
    zz.expcap[slot] = cap;
    zz.depth--;
    zz.expanding--;

    if (!ok) {
        if (zz.path != zz.errpath) {
            int i = 0;
            while (i + 1 < (int) sizeof(zz.errpath) && zz.path[i] != 0) {
                zz.errpath[i] = zz.path[i];
                i++;
            }
            zz.errpath[i] = 0;
            zz.path = zz.errpath;
        }

        return false;
    }
    zz.path = saved_path;
    zz.line = saved_line;

    return true;
}

/* ---------------------------------------------------------- directives */


/* How deep INCLUDE may go, and how much buffer a file below the first gets.
 *
 * The top-level file keeps BUF_KB, because that is what sets the longest line
 * a source may have. An included file gets less: four kilobytes is still far
 * more than any real line, and eight of them is 32 KB rather than 128. */
#define INCLUDE_BUF_KB   4

/* Case-insensitive against a lower-case literal of known length. Deliberately
 * not `same_ci`: that one is inlined into mnemonic_of and stays that way only
 * while nothing cold calls it, which is a lesson this file has now learned
 * three times. */
static bool dir_is(const char* s, const char* want, int n) {
    for (int i = 0; i < n; i++) {
        if ((s[i] | 0x20) != want[i]) {
            return false;
        }
    }

    return true;
}

static uint8_t directive_of(const char* s, int n) {
    if (*s == '.') {
        s++;
        n--;
    }
    switch (n) {
        case 2:
            if (dir_is(s, "if", 2)) return DIR_IF;
            if (dir_is(s, "db", 2)) return DIR_DB;
            if (dir_is(s, "dw", 2)) return DIR_DW;
            if (dir_is(s, "dl", 2)) return DIR_DL;
            if (dir_is(s, "ds", 2)) return DIR_DS;
            break;
        case 4:
            if (dir_is(s, "else", 4)) return DIR_ELSE;
            if (dir_is(s, "defb", 4)) return DIR_DB;
            if (dir_is(s, "byte", 4)) return DIR_DB;
            if (dir_is(s, "defw", 4)) return DIR_DW;
            if (dir_is(s, "dw24", 4)) return DIR_DL;
            if (dir_is(s, "dw32", 4)) return DIR_DW32;
            if (dir_is(s, "defs", 4)) return DIR_DS;
            if (dir_is(s, "blkb", 4)) return DIR_BLKB;
            if (dir_is(s, "blkw", 4)) return DIR_BLKW;
            if (dir_is(s, "blkp", 4)) return DIR_BLKP;
            if (dir_is(s, "blkl", 4)) return DIR_BLKL;
            break;
        case 3:
            if (dir_is(s, "org", 3)) return DIR_ORG;
            if (dir_is(s, "cpu", 3)) return DIR_CPU;
            break;
        case 5:
            if (dir_is(s, "endif", 5)) return DIR_ENDIF;
            if (dir_is(s, "macro", 5)) return DIR_MACRO;
            if (dir_is(s, "ascii", 5)) return DIR_DB;
            if (dir_is(s, "asciz", 5)) return DIR_ASCIZ;
            if (dir_is(s, "align", 5)) return DIR_ALIGN;
            break;
        case 6:
            if (dir_is(s, "incbin", 6)) return DIR_INCBIN;
            if (dir_is(s, "assume", 6)) return DIR_ASSUME;
            break;
        case 7:
            if (dir_is(s, "include", 7)) return DIR_INCLUDE;
            break;
        case 8:
            if (dir_is(s, "endmacro", 8)) return DIR_ENDMACRO;
            if (dir_is(s, "fillbyte", 8)) return DIR_FILLBYTE;
            if (dir_is(s, "relocate", 8)) return DIR_RELOCATE;
            break;
        case 11:
            if (dir_is(s, "endrelocate", 11)) return DIR_ENDRELOCATE;
            break;
        default:
            break;
    }

    return DIR_NONE;
}

/* One escape, after the backslash. Returns -1 for the ones the reference calls
 * "Illegal escape code in string" -- which is everything not listed, including
 * `\0` and `\x41`, both of which C programmers expect and neither of which the
 * reference takes.
 *
 * The same set inside a character literal: `'\n'` is 0x0A and `'\?'` is 0x3F.
 * That is one table for both, which is what the reference has, and it was
 * checked spelling by spelling rather than assumed -- `\?` had been left out
 * of the string set on the strength of C not having needed it. */
static int str_escape(char c) {
    switch (c) {
        case 'n':  return 0x0A;
        case 't':  return 0x09;
        case 'r':  return 0x0D;
        case 'a':  return 0x07;
        case 'b':  return 0x08;
        case 'f':  return 0x0C;
        case 'v':  return 0x0B;
        case 'e':  return 0x1B;
        case '\\': return 0x5C;
        case '"':  return 0x22;
        case '\'': return 0x27;
        case '?':  return 0x3F;
        default:   return -1;
    }
}

/* A double-quoted string, written out as bytes.
 *
 * Reserved in one go before anything is written: the length is known once the
 * closing quote is found, and escapes only ever shrink it. */
/* Out of line, and measured that way.
 *
 * Inlined into directive_line it shares a frame deep enough that the scan
 * spills its pointer and its cursor on every character -- 35 instructions a
 * character, most of them `ld (ix - 111), hl` and back. A string is one call
 * per item against that. */
__attribute__((noinline))
static bool emit_string(const char** pp, const char* e) {
    const char* p = *pp + 1;                      /* past the opening quote */
    /* No bound on the step past an escape. The buffer ends in a newline one
     * byte past the last valid character, so a backslash in the last position
     * steps over the sentinel and the `q < e` test above ends the scan -- as
     * "string not terminated", which is what it is. */
    const char* q = p;
    while (q < e && *q != '"' && *q != '\n') {
        q += (*q == '\\') ? 2 : 1;
    }
    if (q >= e || *q != '"') {
        zz.err = ZAP_E_STRING_NOT_TERMINATED;

        return false;
    }
    if (!out_reserve_n((int) (q - p))) {
        return false;
    }

    uint8_t* o = zz.o;
    while (p < q) {
        if (*p == '\\') {
            const int v = str_escape(p[1]);
            if (v < 0) {
                zz.err = ZAP_E_BAD_ESCAPE_IN_STRING;

                return false;
            }
            *o++ = (uint8_t) v;
            p += 2;
        } else {
            *o++ = (uint8_t) *p++;
        }
    }
    zz.o = o;
    *pp = q + 1;

    return true;
}

/* A whole item that is nothing but a signed literal, read without the
 * evaluator.
 *
 * Shared by the data directives and by EQU, which had the same fault for the
 * same reason: both went straight to `expr_value` for every value, and an
 * operand has had this path since early on. `DB 42` measured 889 cycles a byte
 * against `ld a, 42`'s 349 before the directives got it; **four EQUs in five
 * are a plain literal** in the corpus, 7,778 of 9,772, and every one of them
 * was paying for a general expression parser.
 *
 * always_inline, and measured that way. Written as an ordinary function it
 * costs `emit_data` a call per item and **isa_db4 went from 381 to 405** --
 * 6.3% to save the duplication, on a loop that runs four times a line. Inlined
 * into both, the directives keep what they had and EQU gets the same path.
 *
 * What makes it safe is what it refuses: the character that ended the digit run has
 * to end the item, so `1+2` goes to the evaluator, and so does `0b1010`, whose
 * run begins with a digit and whose decimal loop stops on the `b`.
 *
 * Hands back where it stopped rather than advancing a pointer through an
 * out-parameter. Taking the address of the caller's cursor is what put it in
 * memory, and that cost the data directives 2.4% even inlined -- the third
 * time in this file that `&p` has been the expensive part of a helper. NULL
 * means it was not a literal and nothing moved. */
__attribute__((always_inline))
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

/* A bare name that ends the item, handed straight to the atom rather than
 * through the evaluator.
 *
 * Same shape and the same reason as lit_value above, for the other half of
 * what a data list holds. `DL label` measured 5,935 cycles a line against
 * `DB 4`'s 3,281, and the difference is not the lookup -- `jp label` costs 73
 * cycles more than `jp 0x040000`, so finding a symbol is nearly free. It is
 * the route: expr_value, expr_term with its unary operators and its forward
 * bookkeeping, and expr_climb called to discover there is no operator.
 *
 * Returns the end of the token, or NULL if this is not one. The first
 * character has to be one a name can start with, which is what keeps `$` and
 * `%1010` out; everything else about the token is left to expr_atom, which
 * already knows a trailing-h hex literal from a label and a local from a
 * global. */
static const char* name_item(const char* p, const char* e) {
    if (!alpha_ch(*p) && *p != '_' && *p != '@') {
        return NULL;
    }
    const char* q = p;
    while (q < e && num_ch(*q)) {
        q++;
    }

    /* What ended the run has to end the item too, or an operator follows and
     * the evaluator is what reads it. */
    const char* r = q;
    while (r < e && is_space_ch(*r)) {
        r++;
    }
    if (*r != ',' && *r != '\n' && *r != ';' && r < e) {
        return NULL;
    }

    return q;
}

/* `DB`, `DW` and `DL`: a comma-separated list of values, and for DB of strings
 * too.
 *
 * A value that names a label still ahead becomes a fixup of the directive's
 * width, which is the same machinery an instruction's immediate uses and the
 * reason widths of one, two and three were already there. */
/* ASCIZ is a second caller, and that is enough for the compiler to stop
 * inlining this into directive_line -- the fault that has cost real time five
 * times in this file. It was measured here and costs nothing: `always_inline`
 * on it reads 5.42s on isa_real and so does leaving it alone, because the call
 * lands on the 3,181 directive lines rather than on all 21,494. Left as it is,
 * rather than forced inline on a rule that does not apply here. */
static bool emit_data(uint8_t width, const char** pp, const char* e) {
    const char* p = *pp;
    for (;;) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p == '"') {
            if (width != 1) {
                /* The reference says "String type not allowed", and means it:
                 * a string is bytes and DW would have to invent a padding
                 * rule. */
                zz.err = ZAP_E_STRING_DB;

                return false;
            }
            if (!emit_string(&p, e)) {
                return false;
            }
        } else {
            evalue value = 0;

            /* A plain literal, read here rather than through the evaluator.
             *
             * `DB 42` measured **889 cycles per byte against `ld a, 42`'s
             * 349** -- two and a half times an instruction that does a
             * mnemonic lookup and a row match on top of the same number --
             * because an operand has had this fast path since early on and a
             * directive went straight to `expr_value` for every item. A list
             * of four costs it four times.
             *
             * The same two forms the operand parser takes, and the same
             * reasons: `0x` and hex digits, or decimal with the first digit
             * outside the loop so that a one-digit value needs no multiply --
             * `d * 10` is a call to __imulu here.
             *
             * Only if what ends the run also ends the item. `DB 1+2` has to go
             * through the evaluator, and deciding that costs one class lookup
             * on a character already in hand. */
            const char* const q = lit_value(p, e, &value);
            if (q != NULL) {
                p = q;
            } else {
                fwd_reset(NULL);
                const char* const nm = name_item(p, e);
                if (nm != NULL) {
                    if (!expr_atom(&value, p, (int) (nm - p))) {
                        return false;
                    }
                    p = nm;
                    /* `q` is already NULL here -- lit_value declined -- which
                     * is what the fixup below tests, and it means the same
                     * thing for a name as for an expression: whatever this
                     * was, it went through the atom and may have left a
                     * forward reference in the slots. */
                } else {
                    uint8_t fwdmask = 0;
                    if (!expr_value(&value, &p, e, &fwdmask)) {
                        return false;
                    }
                }
            }
            if (!out_reserve_n(width)) {
                return false;
            }
            if (q == NULL && expr_fwd != NULL) {
                const sym* target = NULL;
                const sym* sub = NULL;
                bool subneg = false;
                if (!fwd_result(&target, &sub, &subneg)) {
                    return false;
                }
                if (value < ADDEND_MIN || value > ADDEND_MAX) {
                    /* See fixup.addend. The alternative is three bytes of the
                     * constant and a silently wrong fourth, which is worse
                     * than saying so. This is the only place a value wider
                     * than the machine can reach a fixup: an instruction's
                     * immediate is three bytes by the time it gets here. */
                    zz.err = ZAP_E_CONSTANT_TOO_LARGE_ADD_LABEL;

                    return false;
                }
                if (!fix_add(target, sub, (int) value,
                             (uint8_t) (width | (subneg ? FIX_SUB2 : 0)),
                             (int) (zz.o - zz.out))) {
                    return false;
                }
                value = 0;
            }
            /* Written in the machine's word unless the directive is wider
             * than the machine.
             *
             * `value >> 8` on the evaluator's word is a call to __lshru, and
             * DB, DW and DL are 3,181 lines of isa_real between them. Narrowed
             * first, they keep the shifts the eZ80 has. Splitting the write
             * this way is 0.20s of the 0.42 the widening cost -- see
             * .internal/performance-notes.md. */
            uint8_t* o = zz.o;
            if (width > 3) {
                *o++ = (uint8_t) value;
                *o++ = (uint8_t) (value >> 8);
                *o++ = (uint8_t) (value >> 16);
                *o++ = (uint8_t) (value >> 24);
            } else {
                const int v = (int) value;
                *o++ = (uint8_t) v;
                if (width > 1) {
                    *o++ = (uint8_t) (v >> 8);
                }
                if (width > 2) {
                    *o++ = (uint8_t) (v >> 16);
                }
            }
            zz.o = o;
        }

        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p != ',') {
            break;
        }
        p++;
    }
    *pp = p;

    return true;
}

/* Fills `n` bytes with 0xFF, which is what `DS` reserves and what `ALIGN` pads
 * with. Not zero -- that was measured, and it is what an erased ROM reads as. */
static bool emit_fill(int n) {
    if (n <= 0) {
        return true;
    }
    if (!out_reserve_n(n)) {
        return false;
    }

    /* A run that starts where the last one ended is the same run. `DS 4` twice
     * at the end of a file is eight bytes to drop, not four. */
    const int at = (int) (zz.o - zz.out);
    zz.fill_len = (at == zz.fill_end) ? zz.fill_len + n : n;

    zz.filled = true;
    uint8_t* o = zz.o;
    while (n-- != 0) {
        *o++ = zz.fill;
    }
    zz.o = o;
    zz.fill_end = (int) (zz.o - zz.out);

    return true;
}

/* `BLKB n, fill` and its wider relatives: n units of `fill`, written out.
 *
 * Not emit_fill. That one reserves space, which the reference drops if it
 * reaches the end of the file with nothing after it; this writes bytes and is
 * kept wherever it lands. Ending the run is what says so -- `ds 3 / blkb 3` at
 * the end of a file is six bytes in the reference and `blkb 3 / ds 3` is
 * three, so the block is what stops the reservation before it from being
 * dropped.
 *
 * Little-endian at the unit width, and the default fill is 0xFF at that width
 * rather than all ones: `blkw 1` is FF 00 and not FF FF, which is the value
 * 0x00FF written as a word. Measured, not assumed. */
/* n and width are both positive here; the caller has checked the count. */
static bool emit_block(int n, int width, evalue fill) {
    if (n <= 0) {
        return true;
    }
    if (!out_reserve_n(n * width)) {
        return false;
    }

    /* Narrowed once, outside the loop, for the reason emit_data splits its
     * write: three of the four widths fit the machine and only BLKL does not. */
    uint8_t* o = zz.o;
    if (width > 3) {
        while (n-- != 0) {
            *o++ = (uint8_t) fill;
            *o++ = (uint8_t) (fill >> 8);
            *o++ = (uint8_t) (fill >> 16);
            *o++ = (uint8_t) (fill >> 24);
        }
        zz.o = o;

        return true;
    }
    const int f = (int) fill;
    while (n-- != 0) {
        *o++ = (uint8_t) f;
        if (width > 1) {
            *o++ = (uint8_t) (f >> 8);
        }
        if (width > 2) {
            *o++ = (uint8_t) (f >> 16);
        }
    }
    zz.o = o;

    /* Nothing to do about a reservation still pending. The drop at the end of
     * the file only fires when the run ends exactly where the output does, and
     * writing these bytes has already moved that -- which is why `ds 3 / blkb
     * 3` is six bytes in the reference and `blkb 3 / ds 3` is three. */

    return true;
}

/* The quoted file name a file directive takes, copied somewhere it will
 * outlive the line it came from.
 *
 * The copy is not tidiness. `br_open` keeps the name, and `br_resume` reopens
 * by it when an include returns -- so a pointer into the reader's buffer would
 * be a pointer into whatever the *included* file refilled over it. The
 * destination is a local in the caller's frame, which lives exactly as long as
 * the reader that holds it.
 *
 * Double quotes only: the reference calls `INCLUDE 'x.inc'` and `INCLUDE x.inc`
 * both a "String format error", so a bare word is not a file name there. */
static bool file_name(const char** pp, const char* e,
                      char* out, int cap) {
    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (*p != '"') {
        zz.err = ZAP_E_EXPECTED_QUOTED_FILE_NAME;

        return false;
    }
    p++;

    int n = 0;
    while (p < e && *p != '"' && *p != '\n') {
        if (n + 1 >= cap) {
            zz.err = ZAP_E_FILE_NAME_TOO_LONG;

            return false;
        }
        out[n++] = *p++;
    }
    if (*p != '"') {
        zz.err = ZAP_E_STRING_NOT_TERMINATED;

        return false;
    }
    if (n == 0) {
        zz.err = ZAP_E_EXPECTED_FILE_NAME;

        return false;
    }
    out[n] = 0;
    *pp = p + 1;

    return true;
}

/* `INCBIN "file"`: the file's bytes, straight into the output.
 *
 * No reader, no lines, nothing to save and nothing to restore -- it is
 * emit_fill with a file in place of the 0xFF. The size is known before a byte
 * is read, so the room is asked for once and the read goes directly to the
 * cursor rather than through a staging buffer. */
static bool incbin_file(const char* name) {
    const uint8_t fh = mos_fopen(name, FA_READ);
    if (fh == 0) {
        zz.err = ZAP_E_CANNOT_OPEN_FILE;

        return false;
    }
    FIL* fil = mos_getfil(fh);
    if (fil == NULL) {
        mos_fclose(fh);
        zz.err = ZAP_E_CANNOT_OPEN_FILE;

        return false;
    }
    const int n = (int) fil->obj.objsize;
    if (n < 0 || !out_reserve_n(n)) {
        mos_fclose(fh);
        zz.err = ZAP_E_OUT_MEMORY;

        return false;
    }
    if (n > 0) {
        const unsigned got = mos_fread(fh, (char*) zz.o, (unsigned) n);
        if ((int) got != n) {
            mos_fclose(fh);
            zz.err = ZAP_E_CANNOT_READ_FILE;

            return false;
        }
        zz.o += n;
    }
    mos_fclose(fh);

    return true;
}

/* `INCLUDE "file"`: the file's lines, assembled here.
 *
 * The reader lives in `dz` so that the line loop reaches it without an
 * indirection, so an include has to save the one it displaces. That is done
 * here, in this function's own frame, which lives exactly as long as the
 * included file does -- including `name`, whose storage the caller owns for
 * the same reason.
 *
 * The parent's file handle is closed while the child runs and reopened after.
 * MOS has few of them, and `br_suspend`/`br_resume` were built for this: resume
 * seeks back to where the reader had read to, so the parent carries on mid-file
 * without noticing.
 *
 * Paths are opened exactly as written, which is what the reference does: an
 * INCLUDE inside `sub/a.inc` naming `b.inc` gets `./b.inc` there and not
 * `sub/b.inc`. Measured, because it is the sort of thing every assembler
 * decides differently. */
__attribute__((noinline))
static bool include_file(const char* name) {
    if (zz.depth >= INCLUDE_MAXDEPTH) {
        /* A file that includes itself, most likely. Each level costs a frame
         * for this, one for the line loop and one for assemble_line, which is
         * 111 bytes on its own; the machine has no memory protection and would
         * simply stop. */
        zz.err = ZAP_E_INCLUDES_NESTED_TOO_DEEPLY;

        return false;
    }

    const char* const saved_path = zz.path;
    const int saved_line = zz.line;

    /* Suspended *before* the copy is taken, and that order is the whole of it.
     * br_suspend closes the handle and zeroes `fh_`, and br_resume refuses a
     * reader whose `fh_` is not zero -- it reads that as "not suspended". A
     * copy made first carries the old handle back over the zero, so the resume
     * is refused and the handle it names has already been closed. */
    const bool was_file = !zz.rd.mem_;
    if (was_file && !br_suspend(&zz.rd)) {
        zz.err = ZAP_E_CANNOT_SET_FILE_ASIDE;

        return false;
    }
    const buf_reader saved = zz.rd;
    Z_SITE("include reader");
    if (br_open(&zz.rd, name, INCLUDE_BUF_KB) == NULL) {
        zz.rd = saved;
        if (was_file) {
            br_resume(&zz.rd);
        }
        zz.err = ZAP_E_CANNOT_OPEN_FILE;

        return false;
    }
    zz.path = name;
    zz.line = 0;
    zz.depth++;

    /* A conditional belongs to the file it is written in.
     *
     * `IF 1 / INCLUDE "x.inc"` where x.inc has an IF of its own is ordinary --
     * it is how a header switches on what its caller set -- and it was refused
     * here as "conditionals do not nest", because the flag was one per
     * assembly rather than one per file. Both halves of the rule are the
     * reference's: an IF left open at the end of an included file is an error
     * there, and so is an ENDIF in one that would close the caller's. */
    const bool saved_cond = zz.in_cond;
    const bool saved_emit = zz.cond_emit;
    zz.in_cond = false;
    zz.cond_emit = true;

    bool ok = run_lines();
    if (ok && zz.in_cond) {
        zz.err = ZAP_E_IF_LEFT_OPEN_AT_END_FILE;
        ok = false;
    }
    zz.in_cond = saved_cond;
    zz.cond_emit = saved_emit;
    zz.line_mode = saved_emit ? LINE_ASSEMBLE : LINE_SKIP;

    /* The child's buffer goes back whether it worked or not; on the way out of
     * a failure the message has already been kept, and zz.path still names the
     * file it happened in, which is what the report wants. */
    br_destroy(&zz.rd);
    zz.depth--;
    zz.rd = saved;
    if (!ok) {
        /* `zz.path` names this file and points into this frame, which goes
         * away as soon as this returns. The report happens after every frame
         * has unwound, so it is copied somewhere that outlives them. */
        if (zz.path != zz.errpath) {
            int i = 0;
            while (i + 1 < (int) sizeof(zz.errpath) && zz.path[i] != 0) {
                zz.errpath[i] = zz.path[i];
                i++;
            }
            zz.errpath[i] = 0;
            zz.path = zz.errpath;
        }

        return false;
    }
    if (!br_resume(&zz.rd)) {
        zz.err = ZAP_E_CANNOT_REOPEN_FILE;

        return false;
    }
    zz.path = saved_path;
    zz.line = saved_line;

    return true;
}

/* The condition on an IF: an expression, and optionally `== expression`.
 *
 * `==` works only here. Outside an IF the reference calls it "Invalid list
 * format", and `!=`, `<` and `>` are "Illegal operator" everywhere -- so this
 * is the whole of its comparison vocabulary, and none of the corpus's eleven
 * conditional files uses even this one.
 *
 * The value has to be known now. A name that is defined later is "Unknown
 * identifier" in the reference too, which has a second pass and could have
 * waited: deciding what to assemble cannot be deferred by either of us.
 */
static bool cond_value(evalue* out, const char** pp, const char* e) {
    const char* p = *pp;
    fwd_reset(NULL);
    uint8_t fwdmask = 0;
    evalue lhs = 0;
    if (!expr_value(&lhs, &p, e, &fwdmask)) {
        return false;
    }
    if (expr_fwd != NULL) {
        zz.err = ZAP_E_LABEL_DEFINED_ALREADY;

        return false;
    }

    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (p[0] == '=' && p[1] == '=') {
        p += 2;
        if (compat_ez80) {
            /* The reference does not compare. It evaluates the left side and
             * throws the rest of the line away, so `IF 0 == 0` is false there
             * and `IF 1 == 2` is true, and `IF 1 == nosuchname` assembles
             * because the name is never looked at. Measured, all four ways.
             *
             * That is a bug, and a quiet one -- `IF version == 2` means
             * `IF version` -- but it decides which bytes come out, so
             * reproducing it is what `-ez80` is for. The same position as
             * operator precedence, and the second time it has been needed. */
            while (p < e && *p != '\n' && *p != ';') {
                p++;
            }
        } else {
            fwd_reset(NULL);
            evalue rhs = 0;
            if (!expr_value(&rhs, &p, e, &fwdmask)) {
                return false;
            }
            if (expr_fwd != NULL) {
                zz.err = ZAP_E_LABEL_DEFINED_ALREADY;

                return false;
            }
            lhs = lhs == rhs;
        }
    }

    *out = lhs;
    *pp = p;

    return true;
}

/* Defined just below; cond_skip hands the conditional directives on to it. */
__attribute__((noinline))
/* INCLUDE and INCBIN, in a function of their own for the sake of the frame.
 *
 * The name buffer is 80 bytes and has to live for as long as the file it opens
 * does -- `br_open` keeps the pointer and `br_resume` reads it back. Left in
 * directive_line that made the frame 140 bytes, past the 128 an `ix`
 * displacement reaches, so five places in the *other* directives were paying a
 * five-instruction address computation for a buffer they never touch. These
 * two are one line in a thousand; everything else on that path is not. */
__attribute__((noinline))
static bool file_directive(uint8_t kind, const char** pp, const char* e,
                           const char** stop) {
    char name[INCLUDE_NAME_MAX];
    if (!file_name(pp, e, name, (int) sizeof(name))) {
        return false;
    }
    *stop = *pp;

    return kind == DIR_INCBIN ? incbin_file(name) : include_file(name);
}

static const insninfo* suffixed_mnemonic(const char* s, int n,
                                         uint8_t* suffix);
static bool suffixed_insn(const insninfo* insn, uint8_t suffix,
                          const char* p, const char* e, const char** stop);

/* `ASCIZ`: the data list at one byte a value, and then one zero. Not a zero
 * per string -- `asciz "ab", "cd"` is 61 62 63 64 00.
 *
 * Out of line and tested after the data list, so DB, DW and DL reach theirs on
 * the one comparison they always did.
 *
 * That arrangement is not what makes the number: adding this directive costs
 * isa_real 0.08 seconds however it is written. Folded into the data branch
 * with a select, in front of it, behind it, with emit_data forced inline and
 * with it left alone -- all five read 5.42s against 5.34 without it. The cost
 * is directive_line growing and its register allocation moving, which is the
 * same thing that made a file of nothing but `DB 1, 3, 0x07, -1` 3% slower two
 * rounds ago. It is written the way that is easiest to read, since none of the
 * others is faster. */
__attribute__((noinline))
static bool asciz_line(const char** pp, const char* e,
                       const char** stop) {
    if (!emit_data(1, pp, e)) {
        return false;
    }
    if (!out_reserve()) {
        return false;
    }
    *zz.o++ = 0;
    *stop = *pp;

    return true;
}

/* A directive line, or a report that this was not one.
 *
 * Out of line and reached only where the mnemonic lookup failed, so an
 * instruction pays nothing for any of it -- not a test, not a table, not a
 * character. The whole feature sits on the error path of something that used to
 * do nothing but set a message. */
__attribute__((noinline))
static bool directive_line(const char* s, int n, const char* p,
                           const char* e, const char** stop) {
    const uint8_t kind = directive_of(s, n);
    if (kind == DIR_NONE) {
        /* An instruction with a mode suffix, asked here so that the ordinary
         * path keeps the shape it had: assemble_line still ends at
         * `return directive_line(...)` when the mnemonic table says no, and a
         * directive still reaches its own dispatch in one call. Adding a hop
         * in front of this instead cost 1.5% on isa_real, all of it paid by
         * the 3,181 directive lines that are not suffixed instructions. */
        uint8_t suffix = 0;
        const insninfo* const insn = suffixed_mnemonic(s, n, &suffix);
        if (insn != NULL) {
            if (cpu_mask != CPU_EZ80) {
                /* A mode suffix says which of two address widths one
                 * instruction runs in, and neither the Z80 nor the Z180 has
                 * two. The reference refuses all eight spellings under either,
                 * which is eight of its own corpus sources.
                 *
                 * Asked here rather than in suffixed_mnemonic, which returns
                 * NULL for a token that is not a suffixed mnemonic at all --
                 * `read.next` may be a macro. Refusing there would report this
                 * as "unknown instruction" and lose which of the two it was. */
                zz.err = ZAP_E_NO_MODE_SUFFIX_CPU;

                return false;
            }

            return suffixed_insn(insn, suffix, p, e, stop);
        }

        /* A macro, or nothing this understands. Looked up last, after the
         * mnemonic table and the directives, so nothing that is either pays
         * for the walk -- and a macro has to be defined before it is used,
         * which the reference requires too. */
        const macro* m = macro_at(s, n);
        if (m == NULL) {
            err_tok(s, n);
            zz.err = ZAP_E_UNKNOWN_INSTRUCTION;

            return false;
        }

        return macro_expand(m, p, e, stop);
    }

    if (kind == DIR_MACRO) {
        if (!macro_begin(&p, e)) {
            return false;
        }
        *stop = p;

        return true;
    }
    if (kind == DIR_ENDMACRO) {
        /* Only ever reached outside a definition, since inside one the line
         * loop hands it to macro_capture instead. */
        zz.err = ZAP_E_NO_MACRO_OPEN;

        return false;
    }

    if (kind >= DIR_IF) {
        if (kind == DIR_ENDIF || kind == DIR_ELSE) {
            if (!zz.in_cond) {
                /* "Missing IF directive" there, and the same here. */
                zz.err = ZAP_E_NO_IF_OPEN;

                return false;
            }
            if (kind == DIR_ENDIF) {
                zz.in_cond = false;
                zz.cond_emit = true;
            } else {
                /* ELSE toggles, and toggles again: `IF 1 / a / ELSE / b /
                 * ELSE / c / ENDIF` assembles a and c in the reference. */
                zz.cond_emit = !zz.cond_emit;
            }
            zz.line_mode = zz.cond_emit ? LINE_ASSEMBLE : LINE_SKIP;
            *stop = p;

            return true;
        }

        /* IF. Nesting is refused because the reference refuses it, and that is
         * what makes this a flag rather than a stack. */
        if (zz.in_cond) {
            zz.err = ZAP_E_CONDITIONALS_DO_NOT_NEST;

            return false;
        }
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        evalue cond = 0;
        if (!cond_value(&cond, &p, e)) {
            return false;
        }
        zz.in_cond = true;
        zz.cond_emit = cond != 0;
        zz.line_mode = zz.cond_emit ? LINE_ASSEMBLE : LINE_SKIP;
        *stop = p;

        return true;
    }

    if (kind == DIR_CPU) {
        /* A setting, and the whole of it is `cpu_mask`. See there.
         *
         * This was a *check* -- anything but eZ80 refused -- on the reasoning
         * that reproducing the filter meant a second assembler's worth of
         * rows. That was wrong, and wrong about a table that was already in
         * the tree: every Z80, undocumented-Z80 and Z180 row has carried its
         * CPU bits since the day it was written, and match_row has always
         * tested them. Refusing the directive was the only thing standing in
         * front of them.
         *
         * The mode follows the machine. Neither the Z80 nor the Z180 has ADL
         * -- there is no 24-bit mode to be in -- so both select it off, which
         * is what makes `ld hl, 0x1234` three bytes there and four here. The
         * reference does the same; measured, not assumed. */
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        const char* const cs = p;
        while (p < e && name_ch(*p)) {
            p++;
        }
        const int cn = (int) (p - cs);
        if (cn == 4 && same_ci_full("ez80", cs, 4)) {
            cpu_mask = CPU_EZ80;
            zz.adl = ZAP_ADL;
        } else if (cn == 3 && same_ci_full("z80", cs, 3)) {
            cpu_mask = CPU_Z80;
            zz.adl = false;
        } else if (cn == 4 && same_ci_full("z180", cs, 4)) {
            cpu_mask = CPU_Z180;
            zz.adl = false;
        } else {
            /* "Unsupported CPU type" there. The Z280 has a bit in the table
             * and no rows tagged with it, so it is not offered. */
            zz.err = ZAP_E_UNSUPPORTED_CPU_TYPE;

            return false;
        }
        *stop = p;

        return true;
    }

    if (kind == DIR_ENDRELOCATE) {
        if (!zz.reloc) {
            /* "Missing RELOCATE directive" there. */
            zz.err = ZAP_E_NO_RELOCATE_OPEN;

            return false;
        }
        zz.org = zz.reloc_org;
        zz.reloc = false;
        *stop = p;

        return true;
    }

    if (kind == DIR_ASSUME) {
        /* `ASSUME ADL=0` or `=1`, and nothing else: the reference calls any
         * other name an invalid operand and any other value an invalid ADL
         * mode. Spaces are allowed around the equals. */
        if (cpu_mask != CPU_EZ80) {
            /* "No ADL mode for CPU type" there, and for *either* value: the
             * Z80 and the Z180 have no ADL to select, so `ADL=0` is refused
             * as well, though it names the mode they are already in.
             * Measured, all six ways. */
            zz.err = ZAP_E_NO_ADL_MODE_CPU;

            return false;
        }
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (!dir_is(p, "adl", 3) || (cclass[(uint8_t) p[3]] & C_MNEM) != 0) {
            zz.err = ZAP_E_EXPECTED_ADL;

            return false;
        }
        p += 3;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p != '=') {
            zz.err = ZAP_E_EXPECTED_AFTER_ADL;

            return false;
        }
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        /* Read as a number, not as one character: the reference takes
         * `adl=01` and means one by it. Only the two values, though -- `adl=2`
         * is "Invalid ADL mode" there. */
        evalue mode = 0;
        const char* const after = lit_value(p, e, &mode);
        if (after != NULL) {
            p = after;
        } else {
            /* Not only a literal: `assume adl=one` with `one` an EQU is what
             * the reference's own Labels corpus writes, and it was refused
             * here. A label still ahead is refused by both -- the mode decides
             * the width of every immediate below it, so there is nothing
             * sensible to do with one that is not known yet. */
            fwd_reset(NULL);
            uint8_t fwdmask = 0;
            if (!expr_value(&mode, &p, e, &fwdmask)) {
                return false;
            }
            if (expr_fwd != NULL) {
                zz.err = ZAP_E_LABEL_DEFINED_ALREADY;

                return false;
            }
        }
        if (mode != 0 && mode != 1) {
            zz.err = ZAP_E_ADL_0_OR_1;

            return false;
        }
        zz.adl = mode == 1;
        *stop = p;

        return true;
    }

    if (kind >= DIR_INCLUDE) {
        return file_directive(kind, &p, e, stop);
    }

    if (kind <= DIR_DW32) {
        if (!emit_data(kind, &p, e)) {
            return false;
        }
        *stop = p;

        return true;
    }

    /* After the data list and not before it, so that DB, DW, DL and DW32 reach
     * theirs on the one comparison they always did. */
    if (kind == DIR_ASCIZ) {
        return asciz_line(&p, e, stop);
    }

    /* DS and ALIGN both take one value that has to be known now -- the
     * reference refuses a label still ahead of either, having no way to reserve
     * an amount it does not know yet. */
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    /* The evaluator's word, narrowed at each use below. A count, an address
     * and a fill byte are all the machine's, but they arrive through the
     * evaluator and one of them -- BLKL's fill -- is four bytes wide. See
     * evalue. */
    evalue value = 0;

    /* A plain number, read here rather than through the evaluator, for the
     * same reason a data item is: `DS 4` measured 5,603 cycles a line against
     * `DB 4`'s 3,281, and a count is a number far more often than it is
     * anything else. `ORG $ + 8` and `ALIGN size*2` still go the long way.
     *
     * lit_value wants what ends the run to end the item, and for these the
     * item is the whole rest of the line -- which it already takes, since a
     * newline or a remark ends an item too. `DS 3,1,2` is the one form where
     * a comma follows, and the arguments after the count are taken and
     * ignored, so stopping at the comma is right there as well. */
    const char* const lit = lit_value(p, e, &value);
    if (lit != NULL) {
        p = lit;
    } else {
        fwd_reset(NULL);
        uint8_t fwdmask = 0;
        if (!expr_value(&value, &p, e, &fwdmask)) {
            return false;
        }
        if (expr_fwd != NULL) {
            zz.err = ZAP_E_LABEL_DEFINED_ALREADY;

            return false;
        }
    }

    if (kind == DIR_DS) {
        /* Refused rather than reproduced. The reference reads the count as
         * unsigned, so `DS -1` there is sixteen megabytes of 0xFF and a
         * successful assembly; on a 512 KB machine that is a way to lose the
         * program rather than a feature, and there is no byte sequence worth
         * agreeing with. Same position as division by zero. */
        if (value < 0) {
            zz.err = ZAP_E_DS_POSITIVE_NUMBER;

            return false;
        }
        if (!emit_fill((int) value)) {
            return false;
        }
        /* `DS 3,1,2` is three bytes in the reference: the arguments after the
         * count are taken and ignored. Skipped rather than parsed, since
         * nothing reads them. */
        while (p < e && *p != '\n' && *p != ';') {
            p++;
        }
        *stop = p;

        return true;
    }

    if (kind == DIR_FILLBYTE) {
        /* One byte of it, and it stands for the rest of the assembly.
         *
         * In the reference it stands for the *whole* assembly, backwards as
         * well: `ds 2 / fillbyte 0xAA` fills that earlier reservation with
         * 0xAA, because a reservation there is a gap that is filled when the
         * file is written out and the last FILLBYTE wins. One pass writes the
         * bytes where it meets them, so reproducing that means remembering
         * every reserved range to go back over -- 682 of them in isa_real, for
         * a case that appears nowhere in the reference's own corpus, where
         * every FILLBYTE precedes the reservations it is for.
         *
         * So it is refused instead of being got wrong: a FILLBYTE that would
         * change the fill of a reservation already written says so. A second
         * one with the same value is not a change and is allowed. Blocks are
         * unaffected either way -- BLKB writes data, and takes the value in
         * force where it stands, in both assemblers. */
        if (zz.filled && (uint8_t) value != zz.fill) {
            zz.err = ZAP_E_FILLBYTE_COME_BEFORE_SPACE_FILLS;

            return false;
        }
        zz.fill = (uint8_t) value;
        *stop = p;

        return true;
    }

    if (kind == DIR_RELOCATE) {
        if (zz.reloc) {
            /* "Nested relocate not allowed" there. */
            zz.err = ZAP_E_RELOCATE_DOES_NOT_NEST;

            return false;
        }

        if (value < 0 || value > 0xFFFFFF) {
            /* "Address outside 24-bit range" there, and the eZ80's address
             * space is exactly that, so this is the reference being right
             * rather than a quirk to reproduce. `$1000000` is one past it and
             * `-1` is the other end. */
            zz.err = ZAP_E_ADDRESS_OUTSIDE_24_BIT_RANGE;

            return false;
        }

        /* The bytes stay where they are and the addresses move. `org` is what
         * every address is measured from -- a label, `$`, an EQU taking `$`,
         * and the target of every fixup -- so displacing it is the whole of
         * this directive and nothing else has to know. */
        zz.reloc_org = zz.org;
        zz.reloc = true;
        zz.org = (int) value - (int) (zz.o - zz.out);
        *stop = p;

        return true;
    }

    if (kind >= DIR_BLKB) {
        /* Everything at or above DIR_INCLUDE has returned by here, so this is
         * BLKB, BLKW, BLKP or BLKL, and the width follows from which. */
        if (value < 0) {
            /* Refused for the reason DS is: the reference reads the count as
             * unsigned, so a negative one is sixteen megabytes of fill and a
             * successful assembly. On a 512 KB machine that is a way to lose
             * the program rather than a feature. */
            zz.err = ZAP_E_BLK_POSITIVE_NUMBER;

            return false;
        }
        const int width = kind - DIR_BLKB + 1;

        /* The fill, if one is given. FILLBYTE's value at the unit width if
         * not, which is 0xFF until something says otherwise. */
        evalue fill = zz.fill;
        const sym* pending = NULL;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p == ',') {
            p++;
            while (p < e && is_space_ch(*p)) {
                p++;
            }
            const char* const flit = lit_value(p, e, &fill);
            if (flit != NULL) {
                p = flit;
            } else {
                const char* const fs = p;
                fwd_reset(NULL);
                uint8_t fwdmask = 0;
                if (!expr_value(&fill, &p, e, &fwdmask)) {
                    return false;
                }
                if (expr_fwd != NULL || expr_fwd_bad) {
                    /* The fill names something ahead. It is one value repeated
                     * n times, so there is nothing a per-byte fixup could
                     * usefully do; the run is written now and filled in when
                     * the value is known, which is one record however long it
                     * is. The count is a different matter and is still
                     * refused: how many bytes there are decides where
                     * everything after them lands. */
                    pending = defer_text(fs, (int) (p - fs));
                    if (pending == NULL) {
                        return false;
                    }
                    fill = 0;
                }
            }
        }
        if (pending != NULL && value > 0) {
            if (zz.fillp_used == zz.fillp_cap) {
                Z_SITE("deferred fills");
                const int want = zz.fillp_cap == 0 ? 4 : zz.fillp_cap + zz.fillp_cap;
                fillpatch* grown =
                    (fillpatch*) realloc(zz.fillp, (size_t) want * sizeof(fillpatch));
                if (grown == NULL) {
                    zz.err = ZAP_E_OUT_MEMORY_LABELS;

                    return false;
                }
                zz.fillp = grown;
                zz.fillp_cap = want;
            }
            fillpatch* fp = &zz.fillp[zz.fillp_used++];
            fp->sp = pending;
            fp->off = (int) (zz.o - zz.out);
            fp->count = value;
            fp->width = (uint8_t) width;
            fp->line = zz.line;
        }
        if (!emit_block(value, width, fill)) {
            return false;
        }

        /* Anything after the fill is taken and ignored, as it is for DS. */
        while (p < e && *p != '\n' && *p != ';') {
            p++;
        }
        *stop = p;

        return true;
    }

    if (kind == DIR_ORG) {
        /* The first ORG in a file moves the origin; every later one pads out
         * to its address. That is not a guess -- two ORGs with nothing between
         * them write the 64 KB gap in the reference, so the second is already
         * behaving as a pad even though nothing has been emitted. */
        if (!zz.org_set && zz.o == zz.out) {
            zz.org = value;
        } else {
            const int here = zz.org + (int) (zz.o - zz.out);
            if (value < here) {
                /* "New address lower than current PC address" there, and the
                 * same here: an ORG that goes backwards would have to unwrite
                 * bytes that are already placed. */
                zz.err = ZAP_E_ORG_GOES_BACKWARDS;

                return false;
            }
            if (!emit_fill(value - here)) {
                return false;
            }
            /* Padding to an address is not reserving space: the reference
             * writes it out even at the end of a file, where it drops a DS.
             * Forgetting the run is what says so. */
            zz.fill_len = 0;
        }
        zz.org_set = true;
        *stop = p;

        return true;
    }

    if (value <= 0) {
        zz.err = ZAP_E_ALIGN_POSITIVE_NUMBER;

        return false;
    }
    if ((value & (value - 1)) != 0) {
        zz.err = ZAP_E_ALIGN_POWER_TWO;

        return false;
    }
    /* Pad to the next multiple. `-addr & (n - 1)` is the distance to it, and
     * the AND is a call to __iand on a 24-bit value -- once per ALIGN, which
     * is a price a directive can pay. */
    const int addr = zz.org + (int) (zz.o - zz.out);
    if (!emit_fill((-addr) & (value - 1))) {
        return false;
    }
    *stop = p;

    return true;
}

/* ------------------------------------------------------------- selecting */



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

/* always_inline, and it has to be. It has two callers now -- the ordinary
 * path and the suffixed one -- and a second caller is enough for the compiler
 * to stop inlining it for the first: measured, that put isa_degenerate from
 * 4.86s to 5.80s. The same fault as `same_ci`, the argument parser, sym_define
 * and fwd_result before it. */
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

    /* A first, on its own, and B only if A survives.
     *
     * These were two 0/1 values ANDed together, which reads well and compiles
     * badly: each `(g != 0)` is a compare and a branch to pick between two
     * constants, and both sides were computed before either was looked at.
     * Nothing here needs a 0/1 -- the question is whether the operand shares a
     * bit with what the row accepts, so the bits are tested where they are,
     * and B is reached only by the rows A did not already reject.
     *
     * This is the line to spend care on. Counted over isa_real, 3.40 rows per
     * instruction reach the register test: the rows an instruction wastes
     * time on are mostly rows of the right shape with the wrong registers,
     * not rows of the wrong shape. Of those, A alone rejects 2.00, so B is not
     * computed at all for three rejections in five.
     *
     * No mode test here, and no ccok either. Being in the group is the answer,
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

typedef struct _emitted {
    uint8_t prefix1;
    uint8_t prefix2;
    uint8_t opcode;
} emitted;

/* Register-set masks by byte plane, so a test that was a 24-bit AND -- one
 * call to __iand and one to __lcmpzero -- is one or two byte ANDs. The
 * assertions tie them to the definitions in operand.h, which is the only thing
 * stopping them drifting apart silently. */
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

__attribute__((always_inline)) static inline uint8_t ddfd_prefix(const dop* op) {
    if ((op->r1 & RP1_IX) != 0) {
        return 0xDD;
    }
    if (((op->r1 & RP1_IY) | (op->r2 & RP2_IY)) != 0) {
        return 0xFD;
    }

    return 0;
}

/* The low byte of an immediate, read as a byte.
 *
 * `op->imm & 7` is a 24-bit AND, which is a call to __iand: imm is an int, so
 * the value arrives in hl and the compiler masks it where it sits. Casting to
 * uint8_t first does not help -- the cast is folded away, since masking three
 * bits off a byte and off the whole value give the same answer. What has to
 * change is the load. Reading the low byte through a uint8_t* makes it
 * `ld a, (iy + n); and a, 7`, the byte operation it always was.
 *
 * transform is inlined at both operand sites and two of its cases mask an
 * immediate, so this was four calls per instruction with an immediate.
 *
 * Little-endian, as the hex parser above already assumes. */
static inline uint8_t imm_lo(const dop* op) {
    return *(const uint8_t*) &op->imm;
}

__attribute__((always_inline)) static inline void transform(emitted* out, dop* op, uint8_t type) {
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
                out->opcode |= shl3[imm_lo(op) & 7];
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
            out->opcode |= (uint8_t) op->imm;
            op->mode &= (uint8_t) ~IMM;
            break;
        case TR_BIT:
            out->opcode |= shl3[imm_lo(op) & 7];
            op->mode &= (uint8_t) ~IMM;
            break;
        case TR_SELECT: {
            uint8_t y = 0;
            if (op->imm == 1) {
                y = 2;
            } else if (op->imm == 2) {
                y = 3;
            }
            out->opcode |= shl3[y & 7];
            op->mode &= (uint8_t) ~IMM;
            break;
        }
        default:
            break;
    }
}

static uint8_t* emit_imm(uint8_t* o, const dop* op, uint8_t cond, bool adl) {
    const int width = (cond & IMM_N) ? 1 : (adl ? 3 : 2);

    /* Written out rather than looped, and reading op->imm afresh each time
     * rather than through a local. The loop's `>> (i * 8)` is a variable shift
     * and cost a call to __ishru per byte. A constant shift cast to uint8_t is
     * better but not free: from a local the compiler still calls __ishru for
     * `>> 16`, because the value is in a stack slot it has already loaded as a
     * whole. Left as a field read it is an indexed load of the one byte
     * wanted -- `ld a, (iy+n)` -- for all three. */
    *o++ = (uint8_t) op->imm;
    if (width > 1) {
        *o++ = (uint8_t) (op->imm >> 8);
    }
    if (width > 2) {
        *o++ = (uint8_t) (op->imm >> 16);
    }

    return o;
}

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
 * The output is wrong in the first two, which is the point, and `zz.o` does not
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

__attribute__((always_inline)) static inline bool emit_row(const isa_row* row, dop* a, dop* b, uint8_t suffix) {
    if (!out_reserve()) {
        return false;
    }

    /* One cursor for the whole instruction rather than zz.out[zz.pos++] per
     * byte. put() reloaded both the output base and the position, added them,
     * stored the byte and stored the position back, for every byte written --
     * twenty-three loads of those two fields in this function alone. The
     * reservation above is what makes a bare cursor safe: room for the
     * longest form is already there, so nothing between here and the
     * write-back can move the buffer. */
    ETRUNC_AT(1);

    uint8_t* o = zz.o;

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
            zz.err = ZAP_E_INSTRUCTION_NO_MODE_SUFFIX;

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
        transform(&out, a, row->transformA);
    }
    if (row->transformB != TR_NONE) {
        transform(&out, b, row->transformB);
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

    /* A relative displacement is measured from the instruction after this
     * one, so it is the last thing written and needs no width decision.
     *
     * It was not written at all before: the row's TR_REL transform had no case
     * here, so the operand kept its immediate and was emitted as an ordinary
     * one-byte value -- the target address truncated. `jr 0x040000` assembled
     * to 18 00 where the reference gives 18 fe. Nothing caught it because no
     * benchmark or case file held a relative jump, and the corpus forms that
     * did were filtered out of opcodes.s for failing the byte comparison the
     * filter exists to enforce. */
    if (row->transformA == TR_REL || row->transformB == TR_REL) {
        const dop* rel = (row->transformA == TR_REL) ? a : b;
        if (rel->fwd != NULL) {
            /* Forward: the displacement is not known, so a zero goes down and
             * the fixup carries the address it will be measured from. Whether
             * it is in reach is decided when it is patched. */
            if (!fix_add(rel->fwd, rel->fwd2, rel->imm,
                         rel->fwd2_neg ? FIX_SUB2 : 0,
                         (int) (o - zz.out))) {
                return false;
            }
            *o++ = 0;
        } else {
            const int d = rel->imm - (zz.org + (int) (o - zz.out) + 1);
            if (d < -128 || d > 127) {
                zz.err = ZAP_E_RELATIVE_JUMP_TOO_FAR;

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
#define SFX_WIDE (suffix != 0 ? (suffix & (S_SIS | S_LIS)) == 0 : zz.adl)
        if ((a->mode & IMM) != 0 && (row->condA & (IMM_N | IMM_MMN))) {
            if (a->fwd != NULL
                && !fix_add(a->fwd, a->fwd2, a->imm,
                            (uint8_t) (((row->condA & IMM_N) ? 1
                                                             : (SFX_WIDE ? 3 : 2))
                                       | (a->fwd2_neg ? FIX_SUB2 : 0)),
                            (int) (o - zz.out))) {
                return false;
            }
            o = emit_imm(o, a, row->condA, SFX_WIDE);
        }
        if ((b->mode & IMM) != 0 && (row->condB & (IMM_N | IMM_MMN))) {
            if (b->fwd != NULL
                && !fix_add(b->fwd, b->fwd2, b->imm,
                            (uint8_t) (((row->condB & IMM_N) ? 1
                                                             : (SFX_WIDE ? 3 : 2))
                                       | (b->fwd2_neg ? FIX_SUB2 : 0)),
                            (int) (o - zz.out))) {
                return false;
            }
            o = emit_imm(o, b, row->condB, SFX_WIDE);
        }
#undef SFX_WIDE
    }

    zz.o = o;

    return true;
}

static bool directive_line(const char* s, int n, const char* p,
                           const char* e, const char** stop);

/* A line of a macro being defined: copied in as it stands, unless it is the
 * ENDMACRO that closes it.
 *
 * Nothing else on the line is looked at, which is what lets a body hold names
 * that do not exist yet and arguments that are not values. A MACRO here is
 * refused, as the reference refuses it. */
__attribute__((noinline))
static bool macro_capture(const char* s, int n, const char* p,
                          const char* e, const char** stop) {
    const uint8_t kind = directive_of(s, n);
    if (kind == DIR_ENDMACRO) {
        zz.defining = NULL;
        zz.line_mode = zz.cond_emit ? LINE_ASSEMBLE : LINE_SKIP;
        *stop = p;

        return true;
    }
    if (kind == DIR_MACRO) {
        zz.err = ZAP_E_MACROS_DO_NOT_NEST;

        return false;
    }
    if (!macro_line(s, e)) {
        return false;
    }
    while (p < e && *p != '\n') {
        p++;
    }
    *stop = p;

    return true;
}

/* A line inside a branch that is not being assembled.
 *
 * Nothing on it is looked at except whether it opens, switches or closes the
 * conditional: no label is defined, no EQU, no ORG, no INCLUDE, and no operand
 * is evaluated -- all of which the reference also skips.
 *
 * What the reference does and this does not is check that the mnemonic exists.
 * `IF 0 / garbage / ENDIF` is "Invalid mnemonic" there and assembles here, so
 * a typo inside a branch that is switched off goes unnoticed. That is a
 * widening rather than a byte difference -- there is no program the reference
 * accepts on which the two disagree -- and it is bought by not running a
 * mnemonic lookup on every skipped line. Recorded rather than hidden, because
 * it is a diagnostic the reference gives and this one does not.
 */
__attribute__((noinline))
static bool cond_skip(const char* s, int n, const char* p,
                      const char* e, const char** stop) {
    const uint8_t kind = directive_of(s, n);
    if (kind >= DIR_IF) {
        return directive_line(s, n, p, e, stop);
    }
    while (p < e && *p != '\n') {
        p++;
    }
    *stop = p;

    return true;
}


/* A mnemonic with a mode suffix on it, or a report that this was not one.
 *
 * Reached only where the plain lookup failed, so an ordinary instruction pays
 * nothing for any of it -- not a test, not a character. The token has already
 * been scanned in one piece, because `.` is a mnemonic character: `.db` needs
 * it and so does `ld.lil`, and telling them apart is a dot at a position other
 * than the first.
 *
 * A dot the reader does not understand is left alone rather than refused. That
 * is what sends `.db` and `.assume` to the directives, and it means a macro
 * may be called `read.next` without this deciding otherwise. */
__attribute__((noinline))
static const insninfo* suffixed_mnemonic(const char* s, int n,
                                         uint8_t* suffix) {
    int i = 1;
    while (i < n && s[i] != '.') {
        i++;
    }
    if (i == n) {
        return NULL;
    }
    if (!suffix_bit(&s[i + 1], n - i - 1, zz.adl, suffix)) {
        return NULL;
    }
    return mnemonic_of(s, i);
}

/* The rest of an instruction that carried a mode suffix.
 *
 * The tail is written out again here rather than shared with assemble_line.
 * Sharing it would mean assemble_line calling into it, and that call is what
 * this exists to avoid: a suffix is one line in a thousand and the ordinary
 * path must not pay a call, a frame or a spill for it. It is twenty lines, it
 * is cold, and emit_row folds its suffix handling away in the hot copy while
 * keeping it here. */
__attribute__((noinline))
static bool suffixed_insn(const insninfo* insn, uint8_t suffix,
                          const char* p, const char* e, const char** stop) {
#if defined(TRUNC) || defined(PTRUNC) || defined(LTRUNC) || defined(ETRUNC) \
    || defined(MTRUNC)
    /* A truncated build stops assemble_line part way, and this is a second
     * copy of its tail. Rather than carry every cut twice, a suffixed
     * instruction assembles to nothing in those builds: they exist to measure,
     * and every variant then contains the same nothing, so no difference
     * between two of them holds it. */
    (void) insn;
    (void) suffix;
    while (p < e && *p != '\n') {
        p++;
    }
    *stop = p;

    return true;
#else
    dop a;
    dop b;
    if (!parse_operand(&a, &p, e)) {
        return false;
    }
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (*p == ',') {
        p++;
        if (!parse_operand(&b, &p, e)) {
            return false;
        }
    } else {
        b = dop_none;
    }
    *stop = p;

    const isa_row* const row = match_row(insn, &a, &b);
    if (row == NULL) {
        err_tok(insn->name, insn->len);
        zz.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }

    return emit_row(row, &a, &b, suffix);
#endif
}

/* `RES n, (IX+d), r` and `SET n, (IX+d), r`, and nothing else has three.
 *
 * These are the undocumented Z80 forms that write the result to a register as
 * well as to memory -- `DD CB d 80+r` rather than `DD CB d 86` -- and they are
 * the only instruction shape in the reference with an operand after the second.
 * The table holds them as sixteen pseudo-mnemonics, `res0` through `set7`,
 * each taking `(IX+d)` and a register, so the bit is part of the *name* and
 * two operands is all a row ever needs. Building the name here is what turns
 * the three-operand line into a two-operand one.
 *
 * noinline and reached by a tail call, like the directives and the suffixes:
 * nothing from assemble_line has to survive it, so an ordinary instruction
 * pays one compare on a character already in a register and no frame at all.
 *
 * The bit is checked here because the row cannot: it was spent on the name,
 * so nothing downstream would notice `res 9, (ix+0), b`. A mnemonic that has
 * no numbered form -- `bit`, which writes no result, or `ld` -- simply fails
 * the lookup, which is why this asks the table rather than a list of names. */
__attribute__((noinline))
static bool third_operand(const insninfo* insn, dop* a, dop* b,
                          const char* p, const char* e, const char** stop) {
    char nm[8];
    const int n = insn->len;
    if (n + 1 > (int) sizeof(nm) || !a->noreg || (a->mode & IMM) == 0
        || (a->mode & INDIRECT) != 0 || a->fwd != NULL
        || (unsigned) a->imm > 7) {
        err_tok(insn->name, insn->len);
        zz.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }
    for (int i = 0; i < n; i++) {
        nm[i] = insn->name[i];
    }
    nm[n] = (char) ('0' + a->imm);

    const insninfo* const alt = mnemonic_of(nm, n + 1);
    if (alt == NULL) {
        err_tok(insn->name, insn->len);
        zz.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }

    dop c;
    if (!parse_operand(&c, &p, e)) {
        return false;
    }
    *stop = p;

    const isa_row* const row = match_row(alt, b, &c);
    if (row == NULL) {
        err_tok(insn->name, insn->len);
        zz.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }

    return emit_row(row, b, &c, 0);
}

/* ------------------------------------------------------------------ main */

/* Assembles one line and reports where it stopped.
 *
 * The end of the *line* is not looked for first. It used to be: the caller
 * scanned to the newline to bound this one, and then this one scanned the same
 * bytes again -- two passes over every byte in the source to parse it once.
 * No scan needs that bound, because no scan can run past a newline anyway: it
 * is not a space, not a name character and not part of a number, so every loop
 * stops on it. The caller is told where parsing ended and steps over the
 * newline from there.
 *
 * The bound the scans do carry is `e`, the end of the *buffer*, which the
 * caller already has and which costs nothing to find. That is a different
 * thing from the newline and it is there for a different reason -- see EVERY
 * SCAN IS BOUNDED at the top of this file. */
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
 * .internal/measuring.md, which this exists to serve.
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

__attribute__((noinline)) static bool assemble_line(const char* p, const char* e, const char** stop) {
    /* Bounded, like every scan in the file, and this one was bounded first:
     * it was correct until labels were added around it, nothing about labels
     * touches it, and unbounded it then broke *every line on the Agon* while
     * the host build was perfect.
     *
     * Every line starts here, so the generated assembly is worth re-reading
     * whenever this function changes. `dec iy` before a loop head is the tell,
     * and test/run.sh counts them -- it was three here before the scans were
     * bounded and is zero now. None of those three was actually wrong, which
     * is the point: they were correct by register allocation. */
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    *stop = p;

    /* Nothing, or nothing but a remark. A comment runs to the end of the line,
     * and the caller steps over the newline, so there is nothing to skip here
     * -- no scan of the comment's body at all. */
    if (p >= e || *p == '\n' || *p == ';') {
        return true;
    }
    TRUNC_AT(1, *p);

    const char* s = p;
    while (p < e && (cclass[(uint8_t) *p] & C_MNEM) != 0) {
        p++;
    }
    int n = (int) (p - s);
    if (n == 0) {
        zz.err = ZAP_E_EXPECTED_INSTRUCTION;

        return false;
    }
    TRUNC_AT(2, n);

    /* A line that is not simply assembled: a conditional is switched off, or a
     * macro is being defined and this belongs to its body. One field and one
     * branch to find out, tested after the token and before the label --
     * neither a switched-off branch nor a macro body may define one, and the
     * reference defines neither. */
    if (zz.line_mode != LINE_ASSEMBLE) {
        return zz.line_mode == LINE_CAPTURE
                   ? macro_capture(s, n, p, e, stop)
                   : cond_skip(s, n, p, e, stop);
    }

    /* A label, if a colon follows the name.
     *
     * The reference takes a label at any indent, not only at column 0, so
     * position decides nothing and the colon decides everything. That makes
     * this one test on a line without a label, which is what most lines are.
     *
     * The line may continue: `foo: ld a,b` is a definition and an instruction,
     * and so is `foo:` alone. */
    if (*p == ':') {
        LTRUNC_AT(1);
        const int addr = zz.org + (int) (zz.o - zz.out);
        /* "Label too long" there, at sixty-five characters. The `@` of a
         * local counts towards it, which is why this is asked once for both
         * rather than after the two are told apart.
         *
         * Unsigned, because a length cannot be negative and a signed compare
         * is a `call pe, __setflag` that the codegen budget in test/run.sh
         * counts.
         *
         * This test and the anonymous one below each take assemble_line's
         * frame from 110 bytes to 113, and isa_real from 5.42s to 5.54s. That
         * is the frame and not the tests: the same 128 bytes an `ix`
         * displacement reaches, and the same size of growth that cost 1.8%
         * when an EQU kept a pointer here. Moving both into a function of
         * their own, and moving them into sym_intern, loc_intern and
         * anon_define which are already out of line, both still read 113 --
         * so they are written where the definition is, which is where they
         * are easiest to find. */
        if ((unsigned) n > LABEL_MAX) {
            zz.err = ZAP_E_LABEL_TOO_LONG;

            return false;
        }
        if (*s == '@') {
            /* `@@` is an anonymous label, not a local: it has no name to
             * collide with, so writing it twice is not a redefinition. */
            if (n == 2 && s[1] == '@') {
                if (zz.expanding != 0) {
                    /* "No anonymous labels allowed in macro definition"
                     * there, and refused at the invocation rather than at the
                     * definition, exactly as a global label in a body is. */
                    zz.err = ZAP_E_NO_ANONYMOUS_LABELS_ALLOWED_IN_MAC;

                    return false;
                }
                if (!anon_define(addr)) {
                    return false;
                }
            } else if (loc_define(s, n, addr) == NULL) {
                /* A local. It is not tested against the number formats: the
                 * reference returns before that check for a local, so `@123:`
                 * and `@0ffh:` are labels there and have to be here. */
                return false;
            }
        } else {
            if (numeric_token(s, n)) {
                zz.err = ZAP_E_INVALID_LABEL;

                return false;
            }
            if (zz.expanding != 0) {
                zz.err = ZAP_E_NO_GLOBAL_LABELS_ALLOWED_IN_MACRO;

                return false;
            }
            if (sym_define(s, n, addr) == NULL) {
                return false;
            }
            /* A global ends the scope before it starts a new one -- but not
             * until this line is done with, because the rest of it still
             * belongs to the scope being closed. */
            zz.scope_line = zz.line;
        }
        LTRUNC_AT(2);
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        *stop = p;
        if (p >= e || *p == '\n' || *p == ';') {
            return true;
        }

        /* `name: EQU value`, which names a value rather than an address.
         *
         * Placed after the return above and not before the definition, which
         * is where the two earlier attempts put it. A bare `label:` line never
         * reaches here, and those are 9,347 of isa_memory's lines and every
         * label in isa_real; testing before the definition instead cost 1.8%
         * on isa_memory alone.
         *
         * The label has already been defined at the address the line was at,
         * and equ_line replaces that with the value. It re-finds the symbol by
         * name rather than being handed it: keeping the pointer in a local
         * here took three more bytes of frame, and the frame going 115 to 118
         * was enough to lose `iy` as the line pointer -- 1.8% on every
         * benchmark, including the ones with no EQU in them. One hash lookup
         * per EQU line against a spill on every line in the file. */
        if (is_equ_at(p)) {
            return equ_line(s, n, p, e, stop);
        }

        s = p;
        while (p < e && (cclass[(uint8_t) *p] & C_MNEM) != 0) {
            p++;
        }
        n = (int) (p - s);
        if (n == 0) {
            zz.err = ZAP_E_EXPECTED_INSTRUCTION;

            return false;
        }
    }

    TRUNC_AT(3, n);

    const insninfo* insn = mnemonic_of(s, n);
#ifdef TRUNC_NODIR
    /* Stage 4 without the directive path, so the two halves of that stage can
     * be told apart: the lookup happens on every line, the dispatch on one in
     * eight. */
    TRUNC_AT(4, insn != NULL);
#endif
    if (insn == NULL) {
        /* A directive, a macro, a suffixed instruction, or nothing this
         * understands. All of them are asked here and nowhere earlier, so an
         * instruction line never tests for any of them: `DB` and its dozen
         * relatives, and `.lil` and its seven, live entirely on the path that
         * used to do nothing but set "unknown instruction".
         *
         * And this stays a *tail* call. Written as `insn = something(...)`
         * followed by a fall-through into the operands, everything this
         * function holds has to survive the call, and that cost 6% on every
         * benchmark -- including the ones with no suffix and no directive in
         * them. Nothing returns to this line. */
        return directive_line(s, n, p, e, stop);
    }

    TRUNC_AT(4, insn != NULL);

    dop a;
    dop b;
    if (!parse_operand(&a, &p, e)) {
        return false;
    }
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (*p == ',') {
        p++;
        if (!parse_operand(&b, &p, e)) {
            return false;
        }
        /* A third operand, which only RES and SET have. See third_operand.
         *
         * One compare, on a character already in a register. parse_operand
         * leaves the cursor past whatever follows the operand -- it did so
         * already on three of its four exits and now does on the fourth -- so
         * there is no scan here, and a two-operand line pays a byte compare
         * and nothing else. Written with the scan here instead it was 0.04s
         * of isa_real. */
        if (*p == ',') {
            return third_operand(insn, &a, &b, p + 1, e, stop);
        }
    } else {
        b = dop_none;
    }

    *stop = p;
    TRUNC_AT(5, a.mode + b.mode + a.r0 + b.r0);

    const isa_row* row = match_row(insn, &a, &b);
    if (row == NULL) {
        err_tok(insn->name, insn->len);
        zz.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }

    TRUNC_AT(6, row != NULL);

    return emit_row(row, &a, &b, 0);

#ifdef TRUNC
trunc_done:
    /* The loop wants the line left on its newline. In the seventh build this
     * runs too, over the handful of characters an instruction leaves behind,
     * so every variant carries it and it cancels out of the differences. */
    while (p < e && *p != '\n') {
        p++;
    }
    *stop = p;

    return true;
#endif
}

/* Patches every forward reference, once the whole source has been read.
 *
 * This is what buys the single pass. zap never looks at a line twice, so a
 * reference to a label defined later cannot be resolved where it is read; the
 * output is held in memory in full, so it is patched here instead. The cost of
 * labels is then a line item -- this loop and the table behind it -- rather
 * than a second pass over the source, which would be most of the program
 * again.
 *
 * Little-endian, as everywhere else here. */
/* Settles every expression that was kept as text, before a byte is patched.
 *
 * Everything is defined by now, so the evaluator is simply run again over the
 * copy. A forward reference that survives even this is one to a name nothing
 * ever defined, and is reported against the line that wrote it. */
static bool resolve_deferred(void) {
    for (int i = 0; i < zz.defer_used; i++) {
        defexpr* d = &zz.defer[i];
        const char* p = d->text;
        evalue v = 0;
        uint8_t mask = 0;
        fwd_reset(NULL);
        zz.line = d->line;
        if (!expr_value(&v, &p, d->text + d->len, &mask)) {
            return false;
        }
        if (expr_fwd != NULL || expr_fwd_bad) {
            zz.err = ZAP_E_UNKNOWN_LABEL;

            return false;
        }
        d->sp->addr = v;
        d->sp->defined = true;
    }

    return true;
}

/* Fills in the blocks whose value was not known when they were written. */
static bool resolve_fills(void) {
    for (int i = 0; i < zz.fillp_used; i++) {
        const fillpatch* fp = &zz.fillp[i];
        if (!fp->sp->defined) {
            zz.line = fp->line;
            zz.err = ZAP_E_UNKNOWN_LABEL;

            return false;
        }
        const evalue v = fp->sp->addr;
        uint8_t* o = zz.out + fp->off;
        const int nv = (int) v;
        for (int n = fp->count; n != 0; n--) {
            if (fp->width > 3) {
                *o++ = (uint8_t) v;
                *o++ = (uint8_t) (v >> 8);
                *o++ = (uint8_t) (v >> 16);
                *o++ = (uint8_t) (v >> 24);
                continue;
            }
            *o++ = (uint8_t) nv;
            if (fp->width > 1) {
                *o++ = (uint8_t) (nv >> 8);
            }
            if (fp->width > 2) {
                *o++ = (uint8_t) (nv >> 16);
            }
        }
    }

    return true;
}

static bool resolve_fixups(void) {
    if (!resolve_deferred() || !resolve_fills()) {
        return false;
    }
    for (int i = 0; i < zz.fix_used; i++) {
        if (!patch_fixup(&zz.fixups[i])) {
            return false;
        }
    }

    return true;
}

/* The loop over one file's lines.
 *
 * A function of its own because INCLUDE re-enters it: an included file runs
 * this same loop over its own reader and returns here when it ends. `p` and
 * `end` are locals, so the only thing an include has to save is the reader,
 * which it does in its own frame.
 *
 * Not one line of the loop changed to make that work, and that was the whole
 * point of doing it this way. The alternative -- a stack of readers in `dz`,
 * popped when a file ends -- puts a test for "is there a parent file" in the
 * hottest code in the assembler, to answer a question only an INCLUDE can ask.
 */
__attribute__((noinline)) static bool run_lines(void) {
    /* The cursor is a pointer, not an offset into the buffer.
     *
     * Every line used to turn `bpos_` into a pointer to start, and the pointer
     * back into `bpos_` to finish -- two loads and two adds on the way in, a
     * subtract and a store on the way out, for a value only this loop uses.
     * br_fill_lines never reads bpos_; it only resets it, so nothing needs the
     * offset kept up to date in between. */
    const char* p = zz.rd.buf_;

    /* Empty for a file, so the first pass through the loop fills it. A reader
     * over memory -- which is what a macro expansion is -- has the whole of its
     * content already and can never refill: `br_fill_lines` says so and returns
     * false, which the loop reads as end of file. Without this an expansion
     * assembles to nothing at all, quietly. */
    const char* end = zz.rd.mem_ ? p + zz.rd.bsz_ : p;

    while (true) {
        buf_reader* r = &zz.rd;
        if (p >= end) {
            if (!line_fill(r)) {
                if (zz.err != ZAP_OK) {
                    return false;
                }

                break;
            }
            p = r->buf_;
            end = p + r->bsz_;
        }

        /* The buffer holds whole lines, so this one's newline is in it. */
        const char* stop = p;

        zz.line++;
        if (!assemble_line(p, end, &stop)) {
            /* The innermost failure has already taken `errline`; this line
             * is then the one that invoked it, and its text is the one thing
             * the expansion could not record for itself. Which file and which
             * line it was, it did record -- see macro_expand. */
            if (!zz.errhave) {
                err_line(zz.errline, p, end);
                zz.errhave = true;
            } else if (zz.errfrompath != NULL && zz.errfrom[0] == 0) {
                err_line(zz.errfrom, p, end);
            }

            return false;
        }

            /* Whatever ended the line is here or a step away: parsing stops at
         * the newline, and anything else between is trailing space or a
         * remark.
         *
         * Asked for the newline first, because that is the answer nearly
         * every time and the loop below is expensive to *enter*, never mind
         * to run. The compiler rotates it so the pointer is stored to the
         * frame and shuffled through two register moves on the way to reading
         * one byte -- sixteen instructions to discover there is no trailing
         * space, on every line of the source. One compare replaces them. */
        if (*stop != '\n') {
            /* Walked with a local, and `stop` written once at the end.
             *
             * `stop` has had its address taken -- assemble_line reports
             * through it -- so the compiler cannot keep it in a register and
             * stored it to the frame **on every character of every comment**:
             * `ld (ix - 3), hl` inside the loop, plus two `lea` and a
             * push/pop pair to move the pointer at all. A comment byte cost
             * 48 cycles. A local that is never addressed costs about a third
             * of that, and comments are 28% of the bytes in BBC BASIC. */
            const char* q = stop;
            while (q < end && is_space_ch(*q)) {
                q++;
            }
            if (*q == ';') {
                /* A remark after the instruction. Its body is never looked at
                 * -- the search for the newline here walks it once and that
                 * is all a comment ever costs. */
                while (q < end && *q != '\n') {
                    q++;
                }
            } else if (*q != '\n') {
                zz.err = ZAP_E_UNEXPECTED_TEXT_AFTER_INSTRUCTION;

                return false;
            }
            stop = q;
        }
        /* A line that was only a remark stops at the semicolon, so the rest
         * of it is walked here. This is the whole cost of a comment: one pass
         * over its bytes, looking for the newline and nothing else. */
        /* stop is on the newline: the test above returned for every
         * other case, and the comment skip before it ends on one too.
         * The loop that used to search for it from here could never
         * take a step. */
        /* No bound on the step. The line always ends on a newline that is
         * inside the buffer, or on the sentinel one past it, so stop + 1 is
         * at worst one past the end -- and the refill above tests `p >= end`,
         * which that satisfies just as `end` did. The compare it replaces was
         * a 24-bit one, on every line. */
        p = stop + 1;
    }


    return true;
}

__attribute__((noinline)) static bool run(const char* path) {
    Z_SITE("source reader");
    if (br_open(&zz.rd, path, BUF_KB) == NULL) {
        zz.err = ZAP_E_CANNOT_OPEN_SOURCE;

        return false;
    }

    zz.cap = (int) (zz.rd.fsz_ >> OUT_SHIFT);
    if (zz.cap < OUT_MIN) {
        zz.cap = OUT_MIN;
    }
    Z_SITE("output buffer");
    zz.out = (uint8_t*) malloc((size_t) zz.cap);
    if (zz.out == NULL) {
        zz.err = ZAP_E_OUT_MEMORY;

        return false;
    }
    zz.o = zz.out;
    zz.org = ZAP_ORG;
    zz.org_set = false;
    zz.fill = 0xFF;
    zz.filled = false;
    zz.reloc = false;
    zz.reloc_org = 0;
    zz.adl = ZAP_ADL;
    /* Reset with the rest, and it has to be: `cpu_mask` is a file-scope
     * static so that match_row need not carry it, and the unit tests assemble
     * many sources in one process. Without this, one `.cpu Z80` would decide
     * what every later test in the same run could encode. */
    cpu_mask = CPU_EZ80;
    zz.in_cond = false;
    zz.cond_emit = true;
    zz.line_mode = LINE_ASSEMBLE;
    zz.macros = NULL;
    zz.defining = NULL;
    zz.expanding = 0;
    zz.undo = NULL;
    zz.undo_used = 0;
    zz.undo_cap = 0;
    zz.subfix = NULL;
    zz.subfix_used = 0;
    zz.subfix_cap = 0;
    zz.defer = NULL;
    zz.defer_used = 0;
    zz.defer_cap = 0;
    zz.fillp = NULL;
    zz.fillp_used = 0;
    zz.fillp_cap = 0;
    for (int i = 0; i < INCLUDE_MAXDEPTH; i++) {
        zz.expbuf[i] = NULL;
        zz.expcap[i] = 0;
    }
    zz.lim = zz.out + zz.cap - OUT_MAX_INSN;
    Z_SITE("symbol buckets");
    zz.syms = (symslot*) calloc(NSYMB, sizeof(symslot));
    if (zz.syms == NULL) {
        zz.err = ZAP_E_OUT_MEMORY;

        return false;
    }
    /* One block up front, so sym_define never has to ask whether there is
     * one; it only ever asks whether the newest is full. */
    Z_SITE("symbol blocks");
    zz.blocks = (symblock*) malloc(sizeof(symblock));
    if (zz.blocks == NULL) {
        zz.err = ZAP_E_OUT_MEMORY;

        return false;
    }
    zz.blocks->next = NULL;
    zz.syms_used = 0;
    zz.line = 0;
    zz.path = path;
    zz.depth = 0;

    if (!run_lines()) {
        return false;
    }

    /* The last scope ends with the source, and settles the same way any other
     * one does. Before the globals, because a local that was never defined
     * should be reported against the line that used it rather than after a
     * global's failure somewhere else. */
    if (zz.defining != NULL) {
        /* "Unfinished macro definition" there, and the same here: a body that
         * never closes has swallowed the rest of the file. */
        zz.err = ZAP_E_MACRO_WAS_NEVER_CLOSED;

        return false;
    }

    if (zz.in_cond) {
        /* "Missing ENDIF directive" there, and the same here: a conditional
         * that never closes has silently dropped whatever followed it. */
        zz.err = ZAP_E_IF_WAS_NEVER_CLOSED;

        return false;
    }

    if (!scope_end()) {
        return false;
    }

    if (!resolve_fixups()) {
        return false;
    }

    /* Space that was reserved and never written over is not output. `DS 4` at
     * the end of a file is four bytes shorter in the reference, and `ALIGN 8`
     * at the end is nothing at all -- both measured. Dropped here, once,
     * rather than by testing on every write.
     *
     * After the fixups, not before: a forward reference is patched by offset,
     * and shortening the output first would move nothing but would leave the
     * question of whether it could. */
    if (zz.fill_len != 0 && (int) (zz.o - zz.out) == zz.fill_end) {
        zz.o -= zz.fill_len;
    }

    return true;
}

/* Everything run() may have allocated.
 *
 * Written once because it was written three times: the two error paths and
 * the success path each freed their own list, and the block list was added to
 * one of them. The sanitiser found it; a machine with 512 KB and no leak
 * checker would have found it later and less clearly. */
static void dz_free(void) {
    free(zz.out);
    free(zz.syms);
    free(zz.fixups);
    free(zz.lfixups);
    free(zz.undo);
    free(zz.subfix);
    free(zz.defer);
    free(zz.fillp);
    for (int i = 0; i < INCLUDE_MAXDEPTH; i++) {
        free(zz.expbuf[i]);
    }
    while (zz.macros != NULL) {
        macro* next = zz.macros->next;
        free(zz.macros->body);
        free(zz.macros->marks);
        free(zz.macros);
        zz.macros = next;
    }
    while (zz.names != NULL) {
        namblock* next = zz.names->next;
        free(zz.names);
        zz.names = next;
    }
    /* The local blocks are rewound rather than freed at the end of a scope, so
     * `locnames` may be pointing part way down a list that is still whole.
     * Freed from the first, which is the only pointer that always names the
     * head. */
    while (zz.locnamfirst != NULL) {
        namblock* next = zz.locnamfirst->next;
        free(zz.locnamfirst);
        zz.locnamfirst = next;
    }
    while (zz.blocks != NULL) {
        symblock* next = zz.blocks->next;
        free(zz.blocks);
        zz.blocks = next;
    }
    while (zz.locfirst != NULL) {
        locblock* next = zz.locfirst->next;
        free(zz.locfirst);
        zz.locfirst = next;
    }
    br_destroy(&zz.rd);
}

/* `-ez80`, spelled out rather than compared with same_ci.
 *
 * same_ci is the mnemonic compare and is wanted inlined into mnemonic_of, which
 * runs on every line of the source. Calling it from here -- once, at startup,
 * on an argument -- was enough for the compiler to stop inlining it and emit it
 * out of line, so the hot compare became a call per candidate: about 31,700 of
 * them on isa_real, and **5.5% of runtime**. The measurement that found it is
 * in the notes; the lesson is that a `static inline` helper is inlined at the
 * compiler's discretion, and one cold caller can take that away from every hot
 * one. */
static bool is_ez80_opt(const char* a) {
    return a[0] == '-' && (a[1] | 0x20) == 'e' && (a[2] | 0x20) == 'z'
           && a[3] == '8' && a[4] == '0' && a[5] == 0;
}

/* Colour is asked for, not assumed.
 *
 * The reference emits it unconditionally, which is fine on a terminal and
 * noise everywhere else -- and zap's own corpus runner reads what it prints.
 * Off unless `-color` says otherwise, so a pipe gets text and a person gets
 * the escape codes they wanted. Spelled the same way as the compilers, with
 * `-colour` taken as well, because half the world writes it that way and
 * neither half should have to look it up. */
static bool is_color_opt(const char* a) {
    if (a[0] != '-' || (a[1] | 0x20) != 'c' || (a[2] | 0x20) != 'o'
        || (a[3] | 0x20) != 'l' || (a[4] | 0x20) != 'o') {
        return false;
    }
    if ((a[5] | 0x20) == 'r' && a[6] == 0) {
        return true;
    }

    return (a[5] | 0x20) == 'u' && (a[6] | 0x20) == 'r' && a[7] == 0;
}

/* One flag, taken from anywhere on the line so that `zap -ez80 a.s a.bin` and
 * `zap a.s a.bin -ez80` both work; the reference accepts its own options
 * either side of the filenames and this is meant to drop in.
 *
 * OUT OF LINE, AND NOT FOR TIDINESS. `run` is inlined into main, so main holds
 * the loop over the source lines -- and two hundred instructions of argument
 * handling in front of it moved that loop's register allocation enough to cost
 * **5.3%** on isa_real, on a build where the option is not even given. It is
 * the same wall the mnemonic chain and the symbol chain compare sit behind,
 * reached from the other side: not code that runs, code that is merely
 * *there*. */
__attribute__((noinline)) static bool parse_args(int argc, char* argv[],
                                                 const char** in,
                                                 const char** out) {
    *in = NULL;
    *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (is_ez80_opt(argv[i])) {
                compat_ez80 = true;
            } else if (is_color_opt(argv[i])) {
                use_color = true;
            } else {
                printf("Unknown option %s\r\n", argv[i]);

                return false;
            }
        } else if (*in == NULL) {
            *in = argv[i];
        } else if (*out == NULL) {
            *out = argv[i];
        }
    }
    if (*in == NULL || *out == NULL) {
        printf("Usage: zap [-ez80] [-color] <source> <output>\r\n");

        return false;
    }

    return true;
}

/* The failing line, fetched back out of the file.
 *
 * Most failures happen while the line is still in the reader's buffer and are
 * copied from there. An unresolved label is not one of them: it is found when
 * the fixups are patched, long after the loop has moved on, and the fixup
 * carries a line number precisely because the line itself is gone.
 *
 * Keeping the text on every fixup would answer it -- and there are 843 of
 * them in isa_real, in a record that is sixteen bytes so that indexing it is a
 * shift. Reopening the file costs one open, and it costs it **only when the
 * assembly has already failed**, which is the rule the whole of this
 * machinery is built on.
 *
 * Silent if anything goes wrong. A report that cannot be made is not an
 * error; it is one less line of help. */
static void err_reopen(const char* path, int line) {
    if (path == NULL || line <= 0) {
        return;
    }
    buf_reader r;
    if (br_open(&r, path, 1) == NULL) {
        return;
    }
    int n = 0;
    bool too_long = false;
    while (br_fill_lines(&r, &too_long)) {
        const char* p = r.buf_;
        const char* const e = p + r.bsz_;
        while (p < e) {
            const char* q = p;
            while (q < e && *q != '\n') {
                q++;
            }
            if (++n == line) {
                err_line(zz.errline, p, q);
                zz.errhave = true;
                br_destroy(&r);

                return;
            }
            p = q + 1;
        }
    }
    br_destroy(&r);
}

/* What went wrong, said the way somebody trying to fix it needs to hear it.
 *
 * Three things the one-line form did not have. The **source line**, because a
 * line number sends the reader to the file and the line sends them to the
 * mistake. The **token**, where the site that failed had it in hand. And for
 * a macro, **where it was invoked from** -- without which a failure inside a
 * body reported a line number of a file the reader had to guess at, and a
 * macro invoked in twenty places named none of them.
 *
 * All of it is captured when the failure happens and none of it is maintained
 * in advance, so a source that assembles pays for none of this.
 *
 * Colour on request; see is_color_opt. The codes are the reference's: red for
 * what went wrong, yellow for the text it went wrong in. */
static void report(const char* in) {
    const char* const red = use_color ? "\033[31m" : "";
    const char* const yellow = use_color ? "\033[33m" : "";
    const char* const off = use_color ? "\033[39m" : "";
    const char* const file =
        zz.errfile != NULL ? zz.errfile : (zz.path != NULL ? zz.path : in);

    /* A failure found after the line was read -- an unresolved label -- has
     * no text yet, and the file still has it. */
    if (!zz.errhave && zz.errmacro == NULL) {
        err_reopen(file, zz.line);
    }

    if (zz.errmacro != NULL) {
        printf("%sMacro [%s] in \"%s\" line %d - %s", red, zz.errmacro, file,
               zz.line, zap_err_text[zz.err]);
    } else {
        printf("%sFile \"%s\" line %d - %s", red, file, zz.line,
               zap_err_text[zz.err]);
    }
    if (zz.errat != NULL && zz.erratlen > 0) {
        printf("%s '%.*s'", yellow, zz.erratlen, zz.errat);
    }
    printf("%s\r\n", off);

    /* The line as it was written, indent and all, which is how the reader
     * will find it again. */
    if (zz.errhave) {
        printf("%s%s%s\r\n", yellow, zz.errline, off);
    }
    if (zz.errfrompath != NULL) {
        printf("%sInvoked from \"%s\" line %d as%s\r\n", red,
               zz.errfrompath, zz.errfromline, off);
        if (zz.errfrom[0] != 0) {
            printf("%s%s%s\r\n", yellow, zz.errfrom, off);
        }
    }
}

int main(int argc, char* argv[]) {
    const char* in;
    const char* out;
    if (!parse_args(argc, argv, &in, &out)) {
        return 1;
    }

    /* After the flag is read: the operator table it builds depends on it. */
    build_tables();
    build_cclass();

    printf("Assembling %s\r\n", in);
    const clock_t begin = clock();
    const bool ok = run(in);
    const clock_t end = clock();

    if (!ok) {
        report(in);
        dz_free();

        return 1;
    }

    const uint8_t fh = mos_fopen(out, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        printf("Cannot write %s\r\n", out);
        dz_free();

        return 1;
    }
    const int written = (int) (zz.o - zz.out);
    if (written > 0) {
        mos_fwrite(fh, (char*) zz.out, (uint24_t) written);
    }
    mos_fclose(fh);

    printf("Wrote %s, %d bytes\r\n", out, written);

#ifdef ZMALLOC
    z_report();
#endif

    const uint24_t cs = elapsed_cs(begin, end);
    printf("Done in %u.%02u seconds\r\n", (unsigned) (cs / 100),
           (unsigned) (cs % 100));

    dz_free();

    return 0;
}
