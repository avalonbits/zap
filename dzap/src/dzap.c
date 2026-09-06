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
 * dzap -- direct zap. An assembler for the case that cannot get any easier.
 *
 * The input is nothing but instructions: no labels, no comments, no constants,
 * no directives, no macros, no includes. Every line maps to its bytes and
 * nothing refers to anything else, so there is no symbol table, no fixup
 * stack, no deferred expression and no second look at any byte.
 *
 * The point is a floor. zap spends about 830 cycles per source byte on an
 * Agon, and the question that matters is how much of that is the work of
 * assembling and how much is the machinery around it. Building the same
 * instruction stream with the machinery removed says what the machinery costs
 * -- and then each feature can be added back and priced on its own.
 *
 * So this is deliberately not a general assembler and is not going to become
 * one. Where it takes a shortcut the shortcut is the measurement.
 *
 * What it does share with zap is the part that is genuinely the job: the
 * generated instruction table, and the same rules for matching a row and
 * folding operands into an opcode. Reimplementing those more cheaply would
 * measure a different assembler rather than a floor for this one.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf_reader.h"
#include "isa.h"
#include "operand.h"
#include "timing.h"
#include "value.h"

#ifdef AGONDEV
#include <agon/mos.h>
#else
#include "agon/mos.h"
#endif

/* ADL mode is fixed. Choosing it is a directive, and directives are what this
 * program exists to not have. */
#define DZ_ADL   true
#define DZ_ORG   0x040000

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
    bool indirect;
    bool has_disp;
    int disp;
    bool has_imm;

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

typedef struct _dz {
    buf_reader rd;

    uint8_t* out;
    int pos;
    int cap;

    int line;
    const char* err;
} dz;

/* ---------------------------------------------------------------- output */

static bool out_grow(dz* z) {
    const int want = z->cap + OUT_STEP;
    uint8_t* grown = (uint8_t*) realloc(z->out, (size_t) want);
    if (grown == NULL) {
        return false;
    }
    z->out = grown;
    z->cap = want;

    return true;
}

/* Room for the longest instruction, asked for once.
 *
 * Testing on every byte written meant a bounds check per output byte when an
 * instruction knows in advance that it cannot need more than a handful. The
 * longest form here is two prefixes, an opcode, two displacements and two
 * three-byte immediates. */
#define OUT_MAX_INSN 12

static bool out_reserve(dz* z, int n) {
    while (z->pos + n > z->cap) {
        if (!out_grow(z)) {
            return false;
        }
    }

    return true;
}

/* Only valid after out_reserve has been asked for enough. */
static inline void put(dz* z, uint8_t b) {
    z->out[z->pos++] = b;
}

/* ------------------------------------------------------------- mnemonics */

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
#define NLETTER 27
#define NLEN    8
#define NBUCKET (NLETTER * NLEN)

#define NROW 322

typedef struct rowinfo rowinfo;

struct rowinfo {
    uint8_t modes;
    uint8_t ccok;
    uint8_t a0, a1, a2;
    uint8_t b0, b1, b2;
    uint8_t aempty, bempty;

    /* How far to the next row with a different mode.
     *
     * The rows of an instruction are sorted by mode, so rows that share one
     * are contiguous and a mode that is not wanted is stepped over in a single
     * hop instead of a row at a time. `ld` has 57 rows and 7 distinct modes,
     * and `ld (ix+8), a` used to reach the forty-third of them.
     *
     * Sorting is safe because the mode test is an equality: rows outside the
     * wanted group can never match, and a stable sort leaves the rows inside
     * it in their original order, so the first match is still the same row.
     * The exception is F_CCOK, which lets a row match with a mode that does
     * not -- those four instructions are left unsorted with every skip at 1.
     *
     * The row is held as a pointer rather than an index because the sort moves
     * it away from its position in the instruction's own table, and because
     * `&insn->rows[i]` is a multiply, which is a call. */
    uint8_t skip;

    /* Where to jump to, as a pointer rather than a stride.
     *
     * `ri += skip` looks like the obvious way to write it and costs a call to
     * __imulu, because the stride is a variable times the size of this struct
     * and the eZ80's multiply is 8-bit. Holding the destination costs three
     * bytes per row and makes the jump a plain load. The count is still needed
     * to know when the instruction's rows run out, which is what skip is for.
     *
     * The row is held as a pointer for the same reason -- the sort moves it
     * away from its position in the instruction's own table, and
     * `&insn->rows[i]` is a multiply. */
    const rowinfo* next;
    const isa_row* row;
};

static rowinfo rowtab[NROW];

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
    const rowinfo* rows;
    uint8_t len;
    uint8_t count;
};

static insninfo insntab[512];
static const insninfo* bucket_head[NBUCKET];

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
static uint8_t letter_base[256];

static inline int bucket_of(char first, int n) {
    return letter_base[(uint8_t) first] + (n < NLEN ? n : NLEN - 1);
}

__attribute__((noinline)) static void build_tables(void) {
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
        bucket_head[i] = NULL;
    }
    /* Backwards, so each bucket ends up in table order. */
    for (int i = isa_table_count - 1; i >= 0; i--) {
        const char* name = isa_table[i].name;
        int k = 0;
        while (name[k] != 0) {
            k++;
        }

        insninfo* ins = &insntab[i];
        ins->name = name;
        ins->len = (uint8_t) k;
        ins->count = isa_table[i].count;

        const int b = bucket_of(name[0], k);
        ins->next = bucket_head[b];
        bucket_head[b] = ins;
    }

    /* mnemonic_of compares n characters and does not check the length, which
     * is only safe while every name sharing a bucket has the same length --
     * otherwise `cp` would match the first two characters of `cpi`, and the
     * table is full of such prefixes. That holds because the bucket key
     * includes the length and no mnemonic is long enough to reach the clamp.
     * Both of those are somebody else's decision to change, so it is checked
     * here rather than assumed, and the CLI test looks for this line. */
    for (int b = 0; b < NBUCKET; b++) {
        for (const insninfo* x = bucket_head[b]; x != NULL; x = x->next) {
            for (const insninfo* y = x->next; y != NULL; y = y->next) {
                if (x->len != y->len) {
                    printf("isa table: %s and %s share a bucket\r\n",
                           x->name, y->name);
                }
            }
        }
    }

    int r = 0;
    for (int i = 0; i < isa_table_count; i++) {
        const isa_insn* insn = &isa_table[i];
        const int base = r;
        insntab[i].rows = &rowtab[base];

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
            ri->skip = 1;
            ri->next = ri + 1;
            ri->row = row;
            r++;
        }

        if (any_cc) {
            /* A row that takes a condition code can match with a mode that
             * does not, so it must be reached whatever the operands were.
             * There are four such rows in the whole table and none of their
             * instructions has more than four rows, so they scan linearly. */
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

        for (int j = base; j < r; ) {
            int e = j;
            while (e < r && rowtab[e].modes == rowtab[j].modes) {
                e++;
            }
            for (int k = j; k < e; k++) {
                rowtab[k].skip = (uint8_t) (e - k);
                rowtab[k].next = &rowtab[e];
            }
            j = e;
        }
    }
}

static inline bool same_ci(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (((a[i] | 0x20)) != ((b[i] | 0x20))) {
            return false;
        }
    }

    return true;
}

/* Packing short names into a word and comparing them in one operation was
 * tried here and was 1.3% slower: bucketing by letter and length already
 * leaves one or two candidates, so the compare loop it replaced was two or
 * three characters long, and building the packed key cost more than that. */
static const insninfo* mnemonic_of(const char* s, int n) {
    for (const insninfo* ins = bucket_head[bucket_of(s[0], n)]; ins != NULL;
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
    0, 0, 0, 1, 0, false, 0, NOREQ, false, false, 0, false, 0
};

/* Scans here are mostly unbounded, and safe because the reader keeps a newline
 * one byte past the last valid one.
 *
 * Every scan below stops at a newline -- none of the character classes it uses
 * contains one -- so the sentinel ends any scan that would otherwise run off
 * the buffer, without a bound being tested on every character. The pointer can
 * reach the end of the content but never pass it, and reading through it there
 * yields the sentinel, which is why the single tests lost their bounds too.
 *
 * The two num_ch scans are the exception and keep theirs; see each. `e` stays
 * a parameter for them. */
static bool parse_operand(dz* z, dop* op, const char** pp, const char* e) {
    *op = dop_none;

    const char* p = *pp;
    while (is_space_ch(*p)) {
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
        op->indirect = true;
        op->mode |= INDIRECT;
        p++;
        while (is_space_ch(*p)) {
            p++;
        }
        cl = cclass[(uint8_t) *p];
    }

    /* A register or flag? */
    if ((cl & C_ALPHA) != 0) {
        const char* s = p;
        while (name_ch(*p)) {
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
            while (is_space_ch(*p)) {
                p++;
            }
            if (op->indirect && (*p == '+' || *p == '-')) {
                const bool neg = *p == '-';
                p++;
                while (is_space_ch(*p)) {
                    p++;
                }
                const char* ds = p;
                /* Bounded, unlike every other scan here, and deliberately.
                 *
                 * Unbounded, this compiles to a loop that is rotated wrongly:
                 * the pointer is pre-decremented and each iteration tests one
                 * character past it, so the first character is never examined
                 * and the scan stops one short. `ld a, 0x42` parses as 0x4 and
                 * leaves `2` behind. The bound is what stops the rotation.
                 *
                 * It does not reduce -- the same loop in isolation compiles
                 * correctly -- and rewriting it to index from a base rather
                 * than advance a pointer does not help; that was tried and
                 * fails the same way. Full diagnosis on the `sentinel`
                 * branch. */
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
                if (digit_ch(*ds)) {
                    const char* q = ds + 1;
                    d = *ds - '0';
                    while (q < p && digit_ch(*q)) {
                        d = d * 10 + (*q - '0');
                        q++;
                    }
                    if (q != p) {
                        z->err = "bad displacement";

                        return false;
                    }
                } else {
                    value dv = 0;
                    if (!num_parse(ds, (int) (p - ds), &dv)) {
                        z->err = "bad displacement";

                        return false;
                    }
                    d = (int) dv;
                }
                op->has_disp = true;
                op->disp = neg ? -d : d;
                while (is_space_ch(*p)) {
                    p++;
                }
            }

            if (op->indirect) {
                if (*p != ')') {
                    z->err = "expected )";

                    return false;
                }
                p++;
            }
            *pp = p;

            return true;
        }

        /* Not a register, and there are no names here to be anything else. */
        z->err = "unknown operand";

        return false;
    }

    /* A literal. */
    {
        const char* s = p;
        if (*p == '-' || *p == '+') {
            p++;
        }
        /* Bounded, for the reason given at the displacement scan above. */
        while (p < e && num_ch(*p)) {
            p++;
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
        int v = 0;
        bool got = false;
        if (nn >= 3 && ns[0] == '0' && (ns[1] | 0x20) == 'x') {
            {
                /* Assembled from the end, a byte at a time, rather than
                 * accumulated as `acc = (acc << 4) | digit`.
                 *
                 * There is no barrel shifter, and the compiler will not turn a
                 * left shift into a byte move even at a byte boundary: `<< 4`
                 * and `<< 8` are both `ld c, n; call __ishl`, a loop over the
                 * bits. That cost about 445 cycles per hex digit, which is why
                 * `ld hl, 0x123456` timed 2.9s slower than `ld a, 0x42` over
                 * thirty thousand lines.
                 *
                 * Working backwards, two digits make a byte with one table
                 * lookup for the high nibble, and the bytes go straight into
                 * the value's own storage. Nothing here shifts anything wider
                 * than a nibble.
                 *
                 * Little-endian, which the eZ80 is and the host is. The
                 * emitter already writes the low byte first for the same
                 * reason. Digits past the third byte are dropped, which is
                 * what the old accumulator did too once it overflowed. */
                union {
                    int v;
                    uint8_t b[sizeof(int)];
                } u;
                u.v = 0;

                /* Three fixed steps rather than a loop with a running byte
                 * index. A value is at most three bytes, so the loop could
                 * only ever run three times, and it was paying for that: a
                 * counter to increment, a bound to test against it, and an
                 * indexed store into the union, which is address arithmetic
                 * on every byte.
                 *
                 * The digits are checked here too, rather than in a pass of
                 * their own. Each used to be looked up twice -- once by a loop
                 * asking whether the run was hex, and again here -- and that
                 * pass cost 811 cycles of this parse's 2,163, and 393 even for
                 * `ld a, 0x42`, where there are two digits. Most of it was the
                 * loop, not the work.
                 *
                 * hexval gives 0xFF for anything that is not a hex digit and a
                 * real nibble is 0x0F or less, so OR-ing the nibbles together
                 * and testing the high half at the end says whether any was
                 * rejected, with no branch per digit. */
                uint8_t bad = 0;
                int j = nn;

                if (j > 2) {
                    uint8_t c = hexval[(uint8_t) ns[--j]];
                    bad |= c;
                    if (j > 2) {
                        const uint8_t hi = hexval[(uint8_t) ns[--j]];
                        bad |= hi;
                        /* Masked: an invalid digit reaches this before `bad`
                         * is tested, and shl4 holds sixteen entries. */
                        c = (uint8_t) (c | shl4[hi & 15]);
                    }
                    u.b[0] = c;
                }
                if (j > 2) {
                    uint8_t c = hexval[(uint8_t) ns[--j]];
                    bad |= c;
                    if (j > 2) {
                        const uint8_t hi = hexval[(uint8_t) ns[--j]];
                        bad |= hi;
                        c = (uint8_t) (c | shl4[hi & 15]);
                    }
                    u.b[1] = c;
                }
                if (j > 2) {
                    uint8_t c = hexval[(uint8_t) ns[--j]];
                    bad |= c;
                    if (j > 2) {
                        const uint8_t hi = hexval[(uint8_t) ns[--j]];
                        bad |= hi;
                        c = (uint8_t) (c | shl4[hi & 15]);
                    }
                    u.b[2] = c;
                }

                /* Digits past the third byte are dropped from the value but
                 * must still be rejected if they are not hex, or a literal the
                 * reference refuses would assemble here. Only a literal of
                 * more than six digits reaches this. */
                while (j > 2) {
                    bad |= hexval[(uint8_t) ns[--j]];
                }

                if ((bad & 0xF0) == 0) {
                    v = u.v;
                    got = true;
                }
            }
        } else if (nn > 0 && digit_ch(ns[0])) {
            /* First digit outside the loop, for the reason given at the
             * displacement above: a one-digit literal then needs no multiply,
             * and `im 2`, `rst 0`, `bit 3` and the rest of the small decimals
             * are exactly that. */
            int acc = ns[0] - '0';
            int k = 1;
            for (; k < nn; k++) {
                if (!digit_ch(ns[k])) {
                    break;
                }
                acc = acc * 10 + (ns[k] - '0');
            }
            if (k == nn) {
                v = acc;
                got = true;
            }
        }
        if (!got) {
            value gv = 0;
            if (nn <= 0 || !num_parse(ns, nn, &gv)) {
                z->err = "expected a value";

                return false;
            }
            v = (int) gv;
        }
        op->imm = neg ? -v : v;
        op->has_imm = true;
        op->mode |= IMM;

        while (is_space_ch(*p)) {
            p++;
        }
        if (op->indirect) {
            if (*p != ')') {
                z->err = "expected )";

                return false;
            }
            p++;
        }
        *pp = p;
    }

    return true;
}

/* ------------------------------------------------------------- selecting */



static const isa_row* match_row(const insninfo* insn,
                                                         const dop* a,
                                                         const dop* b) {
    const uint8_t want = (uint8_t) (shl4[a->mode & 15] | (b->mode & 15));
    const uint8_t has_cc = (uint8_t) (a->cc != 0);
    const rowinfo* ri = insn->rows;

    /* Already split, by whoever recognised the register. */
    const uint8_t a0 = a->r0, a1 = a->r1, a2 = a->r2;
    const uint8_t b0 = b->r0, b1 = b->r1, b2 = b->r2;
    const uint8_t anone = a->noreg;
    const uint8_t bnone = b->noreg;

    for (uint8_t i = 0; i < insn->count; ) {
        /* The cheapest discriminator first, and it is allowed to end the
         * candidate outright.
         *
         * Every term used to be evaluated for every row so the whole test
         * could be one branch -- which is the right shape when the terms cost
         * the same. They do not: the mode test is one compare of a precomputed
         * value, and reg_match is the most expensive line in the program, run
         * twice. Three or four rows are scanned per instruction and all but one
         * are rejected, so paying the expensive half only for rows that
         * survive the cheap half is worth the branch, even on a chip that does
         * not predict them. Measured, not assumed.
         *
         * A row that takes a condition code can still match with a mode that
         * does not, which is why the second half of the old expression has to
         * be part of the rejection rather than after it. */
        const uint8_t ccok = ri->ccok;
        if (ri->modes != want && !(ccok & has_cc)) {
            /* Past the whole group sharing this mode, not just this row. */
            i = (uint8_t) (i + ri->skip);
            ri = ri->next;

            continue;
        }

        const isa_row* row = ri->row;
        const uint8_t ga = (uint8_t) ((ri->a0 & a0) | (ri->a1 & a1)
                                      | (ri->a2 & a2));
        const uint8_t gb = (uint8_t) ((ri->b0 & b0) | (ri->b1 & b1)
                                      | (ri->b2 & b2));
        const uint8_t rega =
            (uint8_t) ((ga != 0) | (ri->aempty & anone) | ccok);
        const uint8_t regb = (uint8_t) ((gb != 0) | (ri->bempty & bnone));

        if (rega & regb) {
            if ((row->cpu & CPU_EZ80) == 0) {
                return NULL;
            }

            return row;
        }

        i++;
        ri++;
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

static inline uint8_t ddfd_prefix(const dop* op) {
    if ((op->r1 & RP1_IX) != 0) {
        return 0xDD;
    }
    if (((op->r1 & RP1_IY) | (op->r2 & RP2_IY)) != 0) {
        return 0xFD;
    }

    return 0;
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
            if (op->has_imm) {
                out->opcode |= shl3[op->imm & 0x07];
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
            op->has_imm = false;
            break;
        case TR_BIT:
            out->opcode |= shl3[op->imm & 0x07];
            op->has_imm = false;
            break;
        case TR_SELECT: {
            uint8_t y = 0;
            if (op->imm == 1) {
                y = 2;
            } else if (op->imm == 2) {
                y = 3;
            }
            out->opcode |= shl3[y & 7];
            op->has_imm = false;
            break;
        }
        default:
            break;
    }
}

static void emit_imm(dz* z, const dop* op, uint8_t cond) {
    const int width = (cond & IMM_N) ? 1 : (DZ_ADL ? 3 : 2);

    /* Written out rather than looped, and reading op->imm afresh each time
     * rather than through a local. The loop's `>> (i * 8)` is a variable shift
     * and cost a call to __ishru per byte. A constant shift cast to uint8_t is
     * better but not free: from a local the compiler still calls __ishru for
     * `>> 16`, because the value is in a stack slot it has already loaded as a
     * whole. Left as a field read it is an indexed load of the one byte
     * wanted -- `ld a, (iy+n)` -- for all three. */
    put(z, (uint8_t) op->imm);
    if (width > 1) {
        put(z, (uint8_t) (op->imm >> 8));
    }
    if (width > 2) {
        put(z, (uint8_t) (op->imm >> 16));
    }
}

__attribute__((noinline)) static bool emit_row(dz* z, const isa_row* row, dop* a, dop* b) {
    if (!out_reserve(z, OUT_MAX_INSN)) {
        return false;
    }

    emitted out;
    out.prefix1 = 0;
    out.prefix2 = row->prefix;
    out.opcode = row->opcode;

    if (row->flags & F_DDFDOK) {
        const uint8_t p1 = ddfd_prefix(a);
        const uint8_t p2 = ddfd_prefix(b);
        out.prefix1 = ((p1 == 0 && p2 != 0) || (!a->indirect && p1 != 0 && p2 != 0))
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

    const bool dd_before_opcode =
        (out.prefix1 == 0xDD || out.prefix1 == 0xFD) && out.prefix2 == 0xCB
        && (row->flags & (F_DISPA | F_DISPB));

    if (out.prefix1 != 0) {
        put(z, out.prefix1);
    }
    if (out.prefix2 != 0) {
        put(z, out.prefix2);
    }
    if (!dd_before_opcode) {
        put(z, out.opcode);
    }
    if (row->flags & F_DISPA) {
        put(z, (uint8_t) (a->disp & 0xFF));
    }
    if (row->flags & F_DISPB) {
        put(z, (uint8_t) (b->disp & 0xFF));
    }
    if (dd_before_opcode) {
        put(z, out.opcode);
    }

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
        const int d = rel->imm - (DZ_ORG + z->pos + 1);
        if (d < -128 || d > 127) {
            z->err = "relative jump too far";

            return false;
        }
        put(z, (uint8_t) d);

        return true;
    }

    if (a->has_imm && (row->condA & (IMM_N | IMM_MMN))) {
        emit_imm(z, a, row->condA);
    }
    if (b->has_imm && (row->condB & (IMM_N | IMM_MMN))) {
        emit_imm(z, b, row->condB);
    }

    return true;
}

/* ------------------------------------------------------------------ main */

/* Assembles one line and reports where it stopped.
 *
 * The end of the line is not looked for first. It used to be: the caller
 * scanned to the newline to bound this one, and then this one scanned the same
 * bytes again -- two passes over every byte in the source to parse it once.
 * Nothing here needs the bound, because no scan can run past a newline
 * anyway: it is not a space, not a name character and not part of a number, so
 * every loop stops on it. The caller is told where parsing ended and steps
 * over the newline from there. */
__attribute__((noinline)) static bool assemble_line(dz* z, const char* p, const char* e, const char** stop) {
    while (is_space_ch(*p)) {
        p++;
    }
    *stop = p;

    /* Nothing, or nothing but a remark. A comment runs to the end of the line,
     * and the caller steps over the newline, so there is nothing to skip here
     * -- no scan of the comment's body at all. */
    if (p >= e || *p == '\n' || *p == ';') {
        return true;
    }

    const char* s = p;
    while ((cclass[(uint8_t) *p] & C_MNEM) != 0) {
        p++;
    }
    const int n = (int) (p - s);
    if (n == 0) {
        z->err = "expected an instruction";

        return false;
    }

    const insninfo* insn = mnemonic_of(s, n);
    if (insn == NULL) {
        z->err = "unknown instruction";

        return false;
    }

    dop a;
    dop b;
    if (!parse_operand(z, &a, &p, e)) {
        return false;
    }
    while (is_space_ch(*p)) {
        p++;
    }
    if (*p == ',') {
        p++;
        if (!parse_operand(z, &b, &p, e)) {
            return false;
        }
    } else {
        b = dop_none;
    }

    *stop = p;

    const isa_row* row = match_row(insn, &a, &b);
    if (row == NULL) {
        z->err = "no such instruction form";

        return false;
    }

    return emit_row(z, row, &a, &b);
}

static bool run(dz* z, const char* path) {
    if (br_open(&z->rd, path, BUF_KB) == NULL) {
        z->err = "cannot open source";

        return false;
    }

    z->cap = (int) (z->rd.fsz_ >> OUT_SHIFT);
    if (z->cap < OUT_MIN) {
        z->cap = OUT_MIN;
    }
    z->out = (uint8_t*) malloc((size_t) z->cap);
    if (z->out == NULL) {
        z->err = "out of memory";

        return false;
    }
    z->pos = 0;
    z->line = 0;

    /* The cursor is a pointer, not an offset into the buffer.
     *
     * Every line used to turn `bpos_` into a pointer to start, and the pointer
     * back into `bpos_` to finish -- two loads and two adds on the way in, a
     * subtract and a store on the way out, for a value only this loop uses.
     * br_fill_lines never reads bpos_; it only resets it, so nothing needs the
     * offset kept up to date in between. */
    const char* p = z->rd.buf_;
    const char* end = p;   /* empty, so the first pass fills */

    while (true) {
        buf_reader* r = &z->rd;
        if (p >= end) {
            bool too_long = false;
            if (!br_fill_lines(r, &too_long)) {
                break;
            }
            p = r->buf_;
            end = p + r->bsz_;
        }

        /* The buffer holds whole lines, so this one's newline is in it. */
        const char* stop = p;

        z->line++;
        if (!assemble_line(z, p, end, &stop)) {
            return false;
        }

            /* Whatever ended the line is here or a step away: parsing stops at the
         * newline, and anything else between is trailing space or a remark. */
        while (is_space_ch(*stop)) {
            stop++;
        }
        if (*stop == ';') {
            /* A remark after the instruction. Its body is never looked at --
             * the search for the newline below walks it once and that is all
             * a comment ever costs. */
            while (*stop != '\n') {
                stop++;
            }
        }
        if (*stop != '\n') {
            z->err = "unexpected text after the instruction";

            return false;
        }
        /* A line that was only a remark stops at the semicolon, so the rest
         * of it is walked here. This is the whole cost of a comment: one pass
         * over its bytes, looking for the newline and nothing else. */
        /* stop is on the newline: the test above returned for every
         * other case, and the comment skip before it ends on one too.
         * The loop that used to search for it from here could never
         * take a step. */
        p = (stop < end) ? stop + 1 : end;
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: dzap <source> <output>\r\n");

        return 1;
    }

    build_tables();
    build_cclass();

    /* Zeroed rather than field by field, because the reader inside it is
     * freed on both paths below and br_open leaves it untouched when it
     * fails. */
    dz z;
    memset(&z, 0, sizeof(z));

    printf("Assembling %s\r\n", argv[1]);
    const clock_t begin = clock();
    const bool ok = run(&z, argv[1]);
    const clock_t end = clock();

    if (!ok) {
        printf("%s line %d: %s\r\n", argv[1], z.line,
               z.err ? z.err : "out of memory for the output");
        free(z.out);
        br_destroy(&z.rd);

        return 1;
    }

    const uint8_t fh = mos_fopen(argv[2], FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        printf("Cannot write %s\r\n", argv[2]);
        free(z.out);
        br_destroy(&z.rd);

        return 1;
    }
    if (z.pos > 0) {
        mos_fwrite(fh, (char*) z.out, (uint24_t) z.pos);
    }
    mos_fclose(fh);

    printf("Wrote %s, %d bytes\r\n", argv[2], z.pos);

    const uint24_t cs = elapsed_cs(begin, end);
    printf("Done in %u.%02u seconds\r\n", (unsigned) (cs / 100),
           (unsigned) (cs % 100));

    free(z.out);
    br_destroy(&z.rd);

    return 0;
}
