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

#include "zap.h"

/* ======================================================================
 * MNEMONICS
 *
 * The instruction table and the lookup that finds a mnemonic in it: rows
 * grouped by operand mode, buckets keyed by first letter and length.
 * ====================================================================== */

#if defined(TRUNC) || defined(PTRUNC) || defined(LTRUNC) || defined(ETRUNC) \
    || defined(MTRUNC)
/* Where a truncated stage sinks what it computed, so the compiler cannot
 * delete the work whose result nothing reads. Declared before every cut, of
 * which there are now five kinds. */
volatile int trunc_sink;
#endif

static rowinfo rowtab[NROW];

static grpinfo grptab[NGRP];

static insninfo insntab[512];

bucketslot bucket_head[NBUCKET];

/* What each row demands of the two operands' modes, as one value.
 *
 * Asking it directly would be `(row->condA & MODECHECK) == modeA` and the same
 * for B: four loads and two masks per candidate row, against something fixed
 * when the table was generated. Both sides are folded here into a single
 * 16-bit value per row, so selecting a row is one compare -- and rows are
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
 * One record rather than parallel arrays: every field is then `ld a, (iy+n)`
 * off the same base, and advancing to the next row is a single lea. Separate
 * arrays would each need their own `ld hl, base; add hl, bc; ld a, (hl)`.
 *
 * The fields are separate bytes rather than packed into wider words. A byte is
 * the native width for all of this: masks are 8-bit instructions here, and ADL
 * mode has no 16-bit truncation, so a 16-bit field makes the compiler mask on
 * every access. */


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
const uint8_t shl3[8] = { 0, 8, 16, 24, 32, 40, 48, 56 };

const uint8_t shl4[16] = {
    0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240
};

/* First letter to the base of its bucket run, precomputed into one table.
 *
 * Computed at run time it would be `letter_of(first) * NLEN`: a case fold, two
 * compares and a multiply, and the multiply is a call to __imulu because the
 * eZ80's MLT is 8-bit. All of it depends on the character alone, and
 * 26 * 8 = 208 fits in a byte. */
/* Which characters begin a binary operator, as a table of its own because
 * cclass has no bit left -- all eight are taken. The question is asked once
 * per operand, where the term ended, and almost every operand is not an
 * expression, so a compare chain of nine would be paid by all of them.
 *
 * `<` and `>` are here but must be doubled: the reference refuses a single
 * one, so `1<4` is an error rather than a comparison. */
uint8_t exop[256];

/* Binding power per operator: two tables, one algorithm.
 *
 * The reference evaluates strictly left to right with no precedence at all --
 * `1+2*3` is 9 there, `2+3*4` is 20, `2*3+4*5` is 50 -- where every other
 * assembler gives 7, 14 and 26. `-ez80` reproduces that by giving every
 * operator the same binding power, because a precedence climb in which
 * everything binds equally *is* a left-to-right fold. The compatible answer
 * falls out of the same code rather than needing its own.
 *
 * The default table is C's ordering, which is what a reader of
 * `mask & flag << 2` expects.
 *
 * Higher binds tighter. */
uint8_t exprec[256];

/* What -o, -b and -a set: the values the assembly starts with, which the
 * source may still move with ORG, FILLBYTE and ASSUME ADL. The defaults are
 * the reference's, which is what makes a command line carrying none of them
 * mean the same thing to both. */
int opt_org = ZAP_ORG;

uint8_t opt_fill = 0xFF;

bool opt_adl = ZAP_ADL;

bool compat_ez80 = false;

/* `-w` (truncation warnings, off by default) is declared above, beside the
 * first site that reads it. */

/* Whether the error report is coloured.
 *
 * On, as it is in the reference, and `-c` turns it off. A drop-in replacement
 * that needs a flag the original did not is not a drop-in replacement, so the
 * default matches even where plain text would be the better guess. The test
 * suite passes `-c`. */
bool use_color = true;

/* What the options ask for beyond the bytes: a listing, a symbol file, the
 * statistics. All of them are written after the assembly is finished except
 * the listing, which is the only one the loop has to know about. */
bool want_list = false;

bool want_console_list = false;

bool want_symbols = false;

bool want_stats = false;

/* Either of the two listings, as one test, so the line loop asks a single
 * question twice a line -- once to remember where the output cursor was, once
 * to print what went between. Those two branches are the whole of what the
 * feature costs a run that did not ask for it. */
bool listing = false;

uint8_t list_fh = 0;

/* Which instruction set is in force, as the bitmask an isa_row carries.
 *
 * `.CPU Z80` and `.CPU Z180` are a filter rather than a second assembler: the
 * same table with the rows another machine does not have taken out of it.
 * Every row carries its CPU bits -- BIT_Z80, BIT_U80 for the undocumented Z80
 * forms, BIT_Z180, BIT_EZ80 -- so the directive is this mask and the two tests
 * in match_row that read it.
 *
 * A file-scope static rather than a field of `zap_state`, for the same reason as
 * `compat_ez80`: match_row is inlined into assemble_line, and anything passed
 * to it is paid for on every instruction in the file. This is set at most once
 * per assembly.
 *
 * eZ80 until a directive says otherwise, which is what an Agon source is. */
uint8_t cpu_mask = CPU_EZ80;

uint8_t letter_base[256];

static inline int bucket_of(char first, int n) {
    return letter_base[(uint8_t) first] + (n < NLEN ? n : NLEN - 1);
}

__attribute__((noinline)) void build_tables(void) {
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
            /* A row that takes a condition code can match operands whose mode
             * does not match the group, so it has to be reached whatever the
             * operands were. call, jp, jr and ret are the four mnemonics this
             * applies to -- ten rows between them -- and they keep a mode test
             * on each row and scan linearly. Leaving those four ungrouped is
             * what lets every other mnemonic drop the test entirely. */
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

/* A whole-name compare including the first character, which `same_ci` skips
 * because its caller has already matched it through the bucket. */
/* The same, without the folding. Macro parameters are matched exactly. */
bool same_full(const char* name, const char* s, int n) {
    for (int i = 0; i < n; i++) {
        if (name[i] != s[i]) {
            return false;
        }
    }

    return true;
}

bool same_ci_full(const char* name, const char* s, int n) {
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
uint8_t suffix_code(uint8_t bit) {
    if (bit == S_SIS) return CODE_SIS;
    if (bit == S_LIS) return CODE_LIS;
    if (bit == S_SIL) return CODE_SIL;

    return CODE_LIL;
}

/* A fold whose label is still ahead: works out where the opcode byte will land
 * and leaves a fixup on it, before the chain in emit_row moves past it.
 *
 * Out of line, because everything here is dead weight in an ordinary
 * instruction and inlining it keeps two more values live across emit_row's
 * body. The prefixes are passed by value rather than as the `emitted` they
 * came from, because taking that struct's address would put it in the
 * frame. */
__attribute__((noinline)) bool fold_defer(uint8_t type, const dop* op,
                                                 uint8_t prefix1, uint8_t prefix2,
                                                 uint8_t flags, int off) {
    if (prefix1 != 0) {
        off++;
    }
    if (prefix2 != 0) {
        off++;
    }
    /* `bit n, (ix+d)` puts the displacement between the CB and the opcode,
     * which is the one shape where the opcode is not the byte after the
     * prefixes. */
    if ((prefix1 == 0xDD || prefix1 == 0xFD) && prefix2 == 0xCB
        && (flags & (F_DISPA | F_DISPB)) != 0) {
        off += ((flags & F_DISPA) != 0) + ((flags & F_DISPB) != 0);
    }

    uint8_t w = FIX_FOLD_BIT;
    if (type == TR_N) {
        w = FIX_FOLD_RST;
    } else if (type == TR_SELECT) {
        w = FIX_FOLD_IM;
    }

    return fix_add(op->fwd, NULL, 0, w, off);
}

uint8_t* emit_imm(uint8_t* o, const dop* op, uint8_t cond, bool adl) {
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

/* A line of a macro being defined: copied in as it stands, unless it is the
 * ENDMACRO that closes it.
 *
 * Nothing else on the line is looked at, which is what lets a body hold names
 * that do not exist yet and arguments that are not values. A MACRO here is
 * refused, as the reference refuses it. */
__attribute__((noinline))
bool macro_capture(const char* s, int n, const char* p,
                          const char* e, const char** stop) {
    const uint8_t kind = directive_of(s, n);
    if (kind == DIR_ENDMACRO) {
        state.defining = NULL;
        state.line_mode = state.cond_emit ? LINE_ASSEMBLE : LINE_SKIP;
        *stop = p;

        return true;
    }
    if (kind == DIR_MACRO) {
        state.err = ZAP_E_MACROS_DO_NOT_NEST;

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
bool cond_skip(const char* s, int n, const char* p,
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
 * nothing for it. The token has already been scanned in one piece, because `.`
 * is a mnemonic character -- `.db` needs that and so does `ld.lil` -- and what
 * tells them apart is a dot at a position other than the first.
 *
 * A dot this does not understand is left alone rather than refused, which is
 * what sends `.db` and `.assume` on to the directives and lets a macro be
 * called `read.next`. */
__attribute__((noinline))
const insninfo* suffixed_mnemonic(const char* s, int n,
                                         uint8_t* suffix) {
    int i = 1;
    while (i < n && s[i] != '.') {
        i++;
    }
    if (i == n) {
        return NULL;
    }
    if (!suffix_bit(&s[i + 1], n - i - 1, state.adl, suffix)) {
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
bool suffixed_insn(const insninfo* insn, uint8_t suffix,
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
        state.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }

    return emit_row(row, &a, &b, suffix);
#endif
}

/* `RES n, (IX+d), r` and `SET n, (IX+d), r`, the only instructions with three
 * operands.
 *
 * They are the undocumented Z80 forms that write the result to a register as
 * well as to memory -- `DD CB d 80+r` rather than `DD CB d 86`. The table
 * holds them as sixteen pseudo-mnemonics, `res0` through `set7`, each taking
 * `(IX+d)` and a register, so the bit number is part of the *name* and no row
 * ever needs more than two operands. Building that name here is what turns a
 * three-operand line into a two-operand one.
 *
 * noinline and reached by a tail call, like the directives and the suffixes:
 * nothing from assemble_line has to survive it, so an ordinary instruction
 * pays one compare on a character already in a register and no frame at all.
 *
 * The bit number is checked here because the row cannot check it -- it was
 * spent on the name, so nothing downstream would notice `res 9, (ix+0), b`. A
 * mnemonic with no numbered form, like `bit` or `ld`, fails the lookup, which
 * is why this asks the table rather than a list of names. */
__attribute__((noinline))
bool third_operand(const insninfo* insn, dop* a, dop* b,
                          const char* p, const char* e, const char** stop) {
    char nm[8];
    const int n = insn->len;
    if (n + 1 > (int) sizeof(nm) || !a->noreg || (a->mode & IMM) == 0
        || (a->mode & INDIRECT) != 0 || a->fwd != NULL
        || (unsigned) a->imm > 7) {
        err_tok(insn->name, insn->len);
        state.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }
    for (int i = 0; i < n; i++) {
        nm[i] = insn->name[i];
    }
    nm[n] = (char) ('0' + a->imm);

    const insninfo* const alt = mnemonic_of(nm, n + 1);
    if (alt == NULL) {
        err_tok(insn->name, insn->len);
        state.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

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
        state.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }

    return emit_row(row, b, &c, 0);
}
