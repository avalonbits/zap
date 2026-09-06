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
    /* 24 bits, not 32.
     *
     * The register set is a bitmask and the highest bit in it is R_I at 2^20,
     * so it fits in the eZ80's native word. Held as uint32_t -- which is what
     * the shared table declares -- every test of it is done across two
     * registers on a machine that has a 24-bit one. reg_match is the single
     * hottest line in the program, run twice for each of three or four
     * candidate rows per instruction, so the difference is paid constantly.
     *
     * The host sees no change at all: uint24_t is uint32_t there. This is one
     * to judge on the Agon figure alone. */
    uint24_t reg;
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
typedef struct {
    uint8_t modes;
    uint8_t ccok;
    uint8_t a0, a1, a2;
    uint8_t b0, b1, b2;
    uint8_t aempty, bempty;
} rowinfo;

static rowinfo rowtab[NROW];
static int16_t row_base[512];

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

static inline int letter_of(char c) {
    const char l = (char) (c | 0x20);

    return (l >= 'a' && l <= 'z') ? (l - 'a') : 26;
}

static inline int bucket_of(char first, int n) {
    return letter_of(first) * NLEN + (n < NLEN ? n : NLEN - 1);
}

__attribute__((noinline)) static void build_tables(void) {
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

/* Packing short names into a word and comparing them in one operation was
 * tried here and was 1.3% slower: bucketing by letter and length already
 * leaves one or two candidates, so the compare loop it replaced was two or
 * three characters long, and building the packed key cost more than that. */
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
static bool reg_of_text(const char* s, int n, uint24_t* bit, uint8_t* index,
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
#define C_MNEM  0x10

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

    /* A mnemonic runs over its suffix too, so the dot belongs to the same run
     * -- asking for it separately made the scan two tests per character. */
    for (int i = 0; i < 256; i++) {
        if (cclass[i] & C_NAME) {
            cclass[i] |= C_MNEM;
        }
    }
    cclass[(uint8_t) '.'] |= C_MNEM;
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

    /* The newline ends the operand list, and so does a remark. Both are
     * checked here rather than by bounding the scan at the end of the line,
     * because finding that end meant a whole extra pass over the source. */
    if (p >= e || *p == ',' || *p == '\n' || *p == ';') {
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

        uint24_t bit = R_NONE;
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
                /* A displacement is one signed byte by the time it is
                 * written, so it is accumulated in the machine's word rather
                 * than the evaluator's 32-bit one. */
                int d = 0;
                if (digit_ch(*ds)) {
                    const char* q = ds;
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
        int v = 0;
        bool got = false;
        if (nn >= 3 && ns[0] == '0' && (ns[1] | 0x20) == 'x') {
            int acc = 0;
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
            int acc = 0;
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



__attribute__((noinline)) static const isa_row* match_row(int idx, const dop* a, const dop* b) {
    const isa_insn* insn = &isa_table[idx];
    const uint8_t want = (uint8_t) (shl4[a->mode & 15] | (b->mode & 15));
    const uint8_t has_cc = (uint8_t) (a->cc != 0);
    const rowinfo* ri = &rowtab[row_base[idx]];

    /* Split once per instruction, not once per row. Read straight out of the
     * operand rather than through a local: a byte of a value already in a
     * register costs a shift, a byte of one still in memory is an indexed
     * load. */
    const uint8_t a0 = (uint8_t) a->reg;
    const uint8_t a1 = (uint8_t) (a->reg >> 8);
    const uint8_t a2 = (uint8_t) (a->reg >> 16);
    const uint8_t b0 = (uint8_t) b->reg;
    const uint8_t b1 = (uint8_t) (b->reg >> 8);
    const uint8_t b2 = (uint8_t) (b->reg >> 16);
    const uint8_t anone = (uint8_t) (a->reg == 0);
    const uint8_t bnone = (uint8_t) (b->reg == 0);

    for (uint8_t i = 0; i < insn->count; i++, ri++) {
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
            continue;
        }

        const isa_row* row = &insn->rows[i];
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
    }

    return NULL;
}

typedef struct _emitted {
    uint8_t prefix1;
    uint8_t prefix2;
    uint8_t opcode;
} emitted;

static inline uint8_t ddfd_prefix(uint24_t reg) {
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

    const char* s = p;
    while (p < e && (cclass[(uint8_t) *p] & C_MNEM) != 0) {
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

    *stop = p;

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

        /* The buffer holds whole lines, so this one's newline is in it. */
        const char* const base = r->buf_;
        const char* const end = &base[r->bsz_];
        const char* stop = &base[r->bpos_];

        z->line++;
        if (!assemble_line(z, &base[r->bpos_], end, &stop)) {
            return false;
        }

            /* Whatever ended the line is here or a step away: parsing stops at the
         * newline, and anything else between is trailing space or a remark. */
        while (stop < end && *stop != '\n' && is_space_ch(*stop)) {
            stop++;
        }
        if (stop < end && *stop == ';') {
            /* A remark after the instruction. Its body is never looked at --
             * the search for the newline below walks it once and that is all
             * a comment ever costs. */
            while (stop < end && *stop != '\n') {
                stop++;
            }
        }
        if (stop < end && *stop != '\n') {
            z->err = "unexpected text after the instruction";

            return false;
        }
        /* A line that was only a remark stops at the semicolon, so the rest
         * of it is walked here. This is the whole cost of a comment: one pass
         * over its bytes, looking for the newline and nothing else. */
        while (stop < end && *stop != '\n') {
            stop++;
        }
        r->bpos_ = (uint24_t) ((stop < end ? stop + 1 : end) - base);
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
