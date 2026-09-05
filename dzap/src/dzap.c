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
    uint32_t reg;
    uint8_t reg_index;
    bool cc;
    uint8_t cc_index;
    uint8_t mode;
    bool indirect;
    bool has_disp;
    int disp;
    bool has_imm;
    value imm;
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

static inline bool emit(dz* z, uint8_t b) {
    if (z->pos == z->cap && !out_grow(z)) {
        return false;
    }
    z->out[z->pos++] = b;

    return true;
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

static int16_t bucket_head[NBUCKET];
static int16_t bucket_next[512];
static uint8_t isa_len[512];

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
static uint16_t row_modes[NROW];
static uint8_t row_ccok[NROW];
static int16_t row_base[512];

static bool tables_ready = false;

static inline int letter_of(char c) {
    const char l = (char) (c | 0x20);

    return (l >= 'a' && l <= 'z') ? (l - 'a') : 26;
}

static inline int bucket_of(char first, int n) {
    return letter_of(first) * NLEN + (n < NLEN ? n : NLEN - 1);
}

static void build_tables(void) {
    if (tables_ready) {
        return;
    }
    tables_ready = true;

    for (int i = 0; i < NBUCKET; i++) {
        bucket_head[i] = -1;
    }
    /* Backwards, so each bucket ends up in table order. */
    for (int i = isa_table_count - 1; i >= 0; i--) {
        const char* name = isa_table[i].name;
        int k = 0;
        while (name[k] != 0) {
            k++;
        }
        isa_len[i] = (uint8_t) k;

        const int b = bucket_of(name[0], k);
        bucket_next[i] = bucket_head[b];
        bucket_head[b] = (int16_t) i;
    }

    int r = 0;
    for (int i = 0; i < isa_table_count; i++) {
        row_base[i] = (int16_t) r;
        for (int j = 0; j < isa_table[i].count; j++) {
            const isa_row* row = &isa_table[i].rows[j];
            row_modes[r] = (uint16_t)
                (((uint16_t) (row->condA & MODECHECK) << 8)
                 | (uint16_t) (row->condB & MODECHECK));
            row_ccok[r] = (uint8_t) ((row->flags & F_CCOK) != 0);
            r++;
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

static int mnemonic_of(const char* s, int n) {
    for (int i = bucket_head[bucket_of(s[0], n)]; i >= 0; i = bucket_next[i]) {
        if (isa_len[i] == n && same_ci(isa_table[i].name, s, n)) {
            return i;
        }
    }

    return -1;
}

/* ------------------------------------------------------- registers, flags */

/* Recognised straight from the text, with the bit and the index the encoder
 * wants. zap reaches these through a token type and then a switch; there is no
 * token here to carry one. */
static bool reg_of_text(const char* s, int n, uint32_t* bit, uint8_t* index,
                        bool* is_cc, uint8_t* cc_index) {
    *is_cc = false;
    *cc_index = 0;

    const char a = (char) (s[0] | 0x20);
    if (n == 1) {
        switch (a) {
            case 'a': *bit = R_A;  *index = 7; return true;
            case 'b': *bit = R_B;  *index = 0; return true;
            case 'c': *bit = R_C;  *index = 1;
                      /* Carry has no name of its own: it would collide with
                       * register C, so the instruction decides which it meant. */
                      *is_cc = true; *cc_index = 3; return true;
            case 'd': *bit = R_D;  *index = 2; return true;
            case 'e': *bit = R_E;  *index = 3; return true;
            case 'h': *bit = R_H;  *index = 4; return true;
            case 'l': *bit = R_L;  *index = 5; return true;
            case 'i': *bit = R_I;  *index = 0; return true;
            case 'r': *bit = R_R;  *index = 0; return true;
            case 'z': *bit = R_NONE; *index = 0; *is_cc = true; *cc_index = 1; return true;
            case 'p': *bit = R_NONE; *index = 0; *is_cc = true; *cc_index = 6; return true;
            case 'm': *bit = R_NONE; *index = 0; *is_cc = true; *cc_index = 7; return true;
            default:  return false;
        }
    }

    if (n == 2) {
        const char b = (char) (s[1] | 0x20);
        switch (a) {
            case 'a': if (b == 'f') { *bit = R_AF; *index = 3; return true; } return false;
            case 'b': if (b == 'c') { *bit = R_BC; *index = 0; return true; } return false;
            case 'd': if (b == 'e') { *bit = R_DE; *index = 1; return true; } return false;
            case 'h': if (b == 'l') { *bit = R_HL; *index = 2; return true; } return false;
            case 's': if (b == 'p') { *bit = R_SP; *index = 3; return true; } return false;
            case 'm': if (b == 'b') { *bit = R_MB; *index = 0; return true; } return false;
            case 'i':
                if (b == 'x') { *bit = R_IX; *index = 2; return true; }
                if (b == 'y') { *bit = R_IY; *index = 2; return true; }

                return false;
            case 'n':
                if (b == 'z') { *bit = R_NONE; *index = 0; *is_cc = true; *cc_index = 0; return true; }
                if (b == 'c') { *bit = R_NONE; *index = 0; *is_cc = true; *cc_index = 2; return true; }

                return false;
            case 'p':
                if (b == 'o') { *bit = R_NONE; *index = 0; *is_cc = true; *cc_index = 4; return true; }
                if (b == 'e') { *bit = R_NONE; *index = 0; *is_cc = true; *cc_index = 5; return true; }

                return false;
            default: return false;
        }
    }

    if (n == 3 && a == 'i') {
        const char b = (char) (s[1] | 0x20);
        const char c = (char) (s[2] | 0x20);
        if (b == 'x') {
            if (c == 'h') { *bit = R_IXH; *index = 4; return true; }
            if (c == 'l') { *bit = R_IXL; *index = 5; return true; }
        } else if (b == 'y') {
            if (c == 'h') { *bit = R_IYH; *index = 4; return true; }
            if (c == 'l') { *bit = R_IYL; *index = 5; return true; }
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

static uint8_t cclass[256];

static void build_cclass(void) {
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
    R_NONE, 0, false, 0, NOREQ, false, false, 0, false, 0
};

static bool parse_operand(dz* z, dop* op, const char** pp, const char* e) {
    *op = dop_none;

    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (p >= e || *p == ',') {
        *pp = p;

        return true;   /* nothing there */
    }

    if (*p == '(') {
        op->indirect = true;
        op->mode |= INDIRECT;
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
    }

    /* A register or flag? */
    if (p < e && name_ch(*p) && !digit_ch(*p)) {
        const char* s = p;
        while (p < e && name_ch(*p)) {
            p++;
        }
        const int n = (int) (p - s);

        uint32_t bit = R_NONE;
        uint8_t index = 0;
        bool is_cc = false;
        uint8_t cc_index = 0;
        if (reg_of_text(s, n, &bit, &index, &is_cc, &cc_index)) {
            op->reg = bit;
            op->reg_index = index;
            if (is_cc) {
                op->cc = true;
                op->cc_index = cc_index;
                if (bit == R_NONE) {
                    /* A flag written as one, which a row asks for with CC. */
                    op->mode |= CC;
                }
            }

            /* (ix+d) and (ix-d). */
            while (p < e && is_space_ch(*p)) {
                p++;
            }
            if (op->indirect && p < e && (*p == '+' || *p == '-')) {
                const bool neg = *p == '-';
                p++;
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
                const char* ds = p;
                while (p < e && num_ch(*p)) {
                    p++;
                }
                value d = 0;
                if (!num_parse(ds, (int) (p - ds), &d)) {
                    z->err = "bad displacement";

                    return false;
                }
                op->has_disp = true;
                op->disp = neg ? -(int) d : (int) d;
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
            }

            if (op->indirect) {
                if (p >= e || *p != ')') {
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
        if (p < e && (*p == '-' || *p == '+')) {
            p++;
        }
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
        value v = 0;
        bool got = false;
        if (nn >= 3 && ns[0] == '0' && (ns[1] | 0x20) == 'x') {
            value acc = 0;
            int k = 2;
            for (; k < nn; k++) {
                const char c = (char) (ns[k] | 0x20);
                if (c >= '0' && c <= '9') {
                    acc = (acc << 4) | (c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    acc = (acc << 4) | (c - 'a' + 10);
                } else {
                    break;
                }
            }
            if (k == nn) {
                v = acc;
                got = true;
            }
        } else if (nn > 0 && digit_ch(ns[0])) {
            value acc = 0;
            int k = 0;
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
        if (!got && (nn <= 0 || !num_parse(ns, nn, &v))) {
            z->err = "expected a value";

            return false;
        }
        op->imm = neg ? -v : v;
        op->has_imm = true;
        op->mode |= IMM;

        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (op->indirect) {
            if (p >= e || *p != ')') {
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

static inline uint8_t reg_match(uint32_t regset, uint32_t reg) {
    return (uint8_t) (((regset & reg) != 0) | ((regset | reg) == 0));
}

static const isa_row* match_row(int idx, const dop* a, const dop* b) {
    const isa_insn* insn = &isa_table[idx];
    const uint16_t want = (uint16_t) (((uint16_t) a->mode << 8) | b->mode);
    const uint8_t has_cc = (uint8_t) (a->cc != 0);
    int r = row_base[idx];

    for (uint8_t i = 0; i < insn->count; i++, r++) {
        const isa_row* row = &insn->rows[i];
        const uint8_t ccok = row_ccok[r];
        const uint8_t rega = (uint8_t) (reg_match(row->regsetA, a->reg) | ccok);
        const uint8_t regb = reg_match(row->regsetB, b->reg);
        const uint8_t cond = (uint8_t)
            ((row_modes[r] == want) | (ccok & has_cc));

        if (rega & regb & cond) {
            if ((row->cpu & CPU_EZ80) == 0) {
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

static inline uint8_t ddfd_prefix(uint32_t reg) {
    if (reg & (R_IX | R_IXH | R_IXL)) {
        return 0xDD;
    }
    if (reg & (R_IY | R_IYH | R_IYL)) {
        return 0xFD;
    }

    return 0;
}

static void transform(emitted* out, dop* op, uint8_t type) {
    switch (type) {
        case TR_IR0:
            if (op->reg & (R_IXL | R_IYL)) {
                out->opcode |= 0x01;
            }
            break;
        case TR_IR3:
            if (op->reg & (R_IXL | R_IYL)) {
                out->opcode |= 0x08;
            }
            break;
        case TR_Z:
            out->opcode |= op->reg_index;
            break;
        case TR_Y:
            if (op->has_imm) {
                out->opcode |= (uint8_t) ((op->imm & 0x07) << 3);
            } else {
                out->opcode |= (uint8_t) (op->reg_index << 3);
            }
            break;
        case TR_P:
            out->opcode |= (uint8_t) (op->reg_index << 4);
            break;
        case TR_CC:
            out->opcode |= (uint8_t) (op->cc_index << 3);
            break;
        case TR_N:
            out->opcode |= (uint8_t) op->imm;
            op->has_imm = false;
            break;
        case TR_BIT:
            out->opcode |= (uint8_t) ((op->imm & 0x07) << 3);
            op->has_imm = false;
            break;
        case TR_SELECT: {
            uint8_t y = 0;
            if (op->imm == 1) {
                y = 2;
            } else if (op->imm == 2) {
                y = 3;
            }
            out->opcode |= (uint8_t) (y << 3);
            op->has_imm = false;
            break;
        }
        default:
            break;
    }
}

static bool emit_imm(dz* z, const isa_row* row, const dop* op, uint8_t cond) {
    int width;
    if (cond & IMM_N) {
        width = 1;
    } else if (cond & IMM_MMN) {
        width = DZ_ADL ? 3 : 2;
    } else {
        width = DZ_ADL ? 3 : 2;
    }
    (void) row;

    for (int i = 0; i < width; i++) {
        if (!emit(z, (uint8_t) ((op->imm >> (i * 8)) & 0xFF))) {
            return false;
        }
    }

    return true;
}

static bool emit_row(dz* z, const isa_row* row, dop* a, dop* b) {
    emitted out;
    out.prefix1 = 0;
    out.prefix2 = row->prefix;
    out.opcode = row->opcode;

    if (row->flags & F_DDFDOK) {
        const uint8_t p1 = ddfd_prefix(a->reg);
        const uint8_t p2 = ddfd_prefix(b->reg);
        out.prefix1 = ((p1 == 0 && p2 != 0) || (!a->indirect && p1 != 0 && p2 != 0))
                      ? p2 : p1;
    }

    transform(&out, a, row->transformA);
    transform(&out, b, row->transformB);

    const bool dd_before_opcode =
        (out.prefix1 == 0xDD || out.prefix1 == 0xFD) && out.prefix2 == 0xCB
        && (row->flags & (F_DISPA | F_DISPB));

    if (out.prefix1 != 0 && !emit(z, out.prefix1)) {
        return false;
    }
    if (out.prefix2 != 0 && !emit(z, out.prefix2)) {
        return false;
    }
    if (!dd_before_opcode && !emit(z, out.opcode)) {
        return false;
    }
    if (row->flags & F_DISPA) {
        if (!emit(z, (uint8_t) (a->disp & 0xFF))) {
            return false;
        }
    }
    if (row->flags & F_DISPB) {
        if (!emit(z, (uint8_t) (b->disp & 0xFF))) {
            return false;
        }
    }
    if (dd_before_opcode && !emit(z, out.opcode)) {
        return false;
    }

    if (a->has_imm && (row->condA & (IMM_N | IMM_MMN))) {
        if (!emit_imm(z, row, a, row->condA)) {
            return false;
        }
    }
    if (b->has_imm && (row->condB & (IMM_N | IMM_MMN))) {
        if (!emit_imm(z, row, b, row->condB)) {
            return false;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ main */

static bool assemble_line(dz* z, const char* p, const char* e) {
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (p >= e) {
        return true;
    }

    const char* s = p;
    while (p < e && (name_ch(*p) || *p == '.')) {
        p++;
    }
    const int n = (int) (p - s);
    if (n == 0) {
        z->err = "expected an instruction";

        return false;
    }

    const int idx = mnemonic_of(s, n);
    if (idx < 0) {
        z->err = "unknown instruction";

        return false;
    }

    dop a;
    dop b;
    if (!parse_operand(z, &a, &p, e)) {
        return false;
    }
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (p < e && *p == ',') {
        p++;
        if (!parse_operand(z, &b, &p, e)) {
            return false;
        }
    } else {
        b = dop_none;
    }

    const isa_row* row = match_row(idx, &a, &b);
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

    while (true) {
        buf_reader* r = &z->rd;
        if (r->bpos_ >= r->bsz_) {
            bool too_long = false;
            if (!br_fill_lines(r, &too_long)) {
                break;
            }
        }

        /* The buffer holds whole lines, so the end of this one is in it. */
        const char* const base = r->buf_;
        uint24_t i = r->bpos_;
        const uint24_t end = r->bsz_;
        while (i < end && base[i] != '\n') {
            i++;
        }

        z->line++;
        if (!assemble_line(z, &base[r->bpos_], &base[i])) {
            return false;
        }

        r->bpos_ = (i < end) ? i + 1 : end;
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

    dz z;
    z.out = NULL;
    z.err = NULL;

    printf("Assembling %s\r\n", argv[1]);
    const clock_t begin = clock();
    const bool ok = run(&z, argv[1]);
    const clock_t end = clock();

    if (!ok) {
        printf("%s line %d: %s\r\n", argv[1], z.line,
               z.err ? z.err : "out of memory for the output");
        free(z.out);

        return 1;
    }

    const uint8_t fh = mos_fopen(argv[2], FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        printf("Cannot write %s\r\n", argv[2]);
        free(z.out);

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
