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
#include "symtab.h"

/* Indexed by the code, so a message and its name cannot drift apart. */
const char* const zap_err_text[] = {
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
    [ZAP_E_ADDRESS_OUTSIDE_16_BIT_RANGE] = "address outside the 16-bit range",
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
    [ZAP_E_INTERRUPT_MODE] = "interrupt mode must be 0, 1 or 2",
    [ZAP_E_INVALID_BIT_NUMBER] = "bit number must be 0 to 7",
    [ZAP_E_INVALID_LABEL] = "invalid label",
    [ZAP_E_LABEL_DEFINED_TWICE] = "label defined twice",
    [ZAP_E_LABEL_TOO_LONG] = "label too long",
    [ZAP_E_LINE_TOO_LONG] = "line too long",
    [ZAP_E_MACRO_NAME_TOO_LONG] = "macro name too long",
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
    [ZAP_E_RESTART_ADDRESS] = "not a restart address",
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

void build_pearson(void) {
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
 * A Pearson hash yields eight bits and the table wants eleven. The usual way
 * to find three more is a second pass with a different seed, which doubles the
 * per-character work -- and that character loop is the key's whole cost.
 *
 * The three extra bits come from the first character, the last and the length
 * instead. All three are already in hand and none of them costs another pass,
 * and they spread the table as well as a second pass does.
 *
 * The length alone is not enough: with only the length, names that are all the
 * same length reach 256 of the 2,048 buckets, and four-character labels then
 * probe six times as often. XORing the two characters in is what rescues it,
 * for two loads. */
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

/* The structural key: first character, last character and length. Not a hash
 * -- a hash walks the name, and the scan that found the token has walked it
 * already.
 *
 * The index is `f * 64 + l * 2 + (len > 6)`. Written that way it would be
 * three library calls: `* 64` is `call __ishl`, and `len > 6` on a signed int
 * is `call pe, __setflag`. Composed a byte at a time from two small tables it
 * is neither.
 *
 * f and l are five bits each, so the low byte holds (f & 3) * 64 plus l * 2
 * plus the length bit -- at most 192 + 62 + 1 -- and the high byte holds
 * f >> 2. Both come from tables because a shift is a call. */
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

/* The assembler's whole state, as one file-scope object.
 *
 * A file-scope object is addressed absolutely: the address of a field is a
 * constant written into the instruction. Reached instead through a pointer
 * parameter, every access would first fetch that pointer out of the frame --
 * hundreds of times in this file, several per source line -- and would tie up
 * `iy`, which the line loop wants for the line pointer.
 *
 * The cost is that there is one assembly per process. Being static also
 * zero-initialises it. */
zap_state state;

/* The failing line, copied out of whatever held it.
 *
 * Called only after something has returned false. Trailing space and the
 * newline come off, so the echo reads as the author wrote it, and a line
 * longer than the buffer is truncated -- the report is a courtesy and must
 * never itself be a failure.
 *
 * Returns nothing and cannot fail, for the same reason. */
void err_line(char* dst, const char* p, const char* e) {
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

/* The same question about an operand's immediate, asked in the machine's word.
 *
 * An instruction's immediate is an `int` -- three bytes on the Agon -- while
 * `fits_width` takes the evaluator's four. Widening every immediate to ask the
 * wider question would turn these compares into calls to __lcmpu, on the path
 * that runs for every line with an operand.
 *
 * In the machine's word the width-three case is provably true and folds away,
 * and the other two are byte and word compares. */
static inline bool fits_imm(int v, int width) {
    /* One add and one unsigned compare.
     *
     * The range that survives `width` bytes is -2^(8w-1) to 2^8w-1. Sliding it
     * down by its lower bound turns the pair of bounds into a single test:
     * `ld a, -1` lands on 127, `ld a, 255` on 383, and anything outside walks
     * off the end.
     *
     * Unsigned throughout, so the addition wraps rather than overflowing and
     * the compare is one subtract with no `call pe, __setflag` after it. */
    if (width == 1) {
        return ((unsigned) v + 128u) <= 383u;
    }
    if (width == 2) {
        return ((unsigned) v + 32768u) <= 98303u;
    }
    /* Three bytes always fits, and this is the reference's rule rather than
     * the machine's word.
     *
     * The reference does not check an instruction's immediate against 24 bits:
     * `ld hl, 0x12345678` is 21 78 56 34 there with nothing said, while the
     * directive `dw24 0x1234567` is a truncation warning. Only the directive
     * path checks, and that is fits_width rather than this. */
    return true;
}

/* Whether `-w` was given, and the one place zap deliberately does not behave
 * like the reference.
 *
 * There, truncation warnings are on and `-i` turns off the printing while the
 * check still runs. Here the check is the cost -- it is the only diagnostic
 * that asks a question of every value in every source rather than doing work
 * after something has gone wrong, and it is worth about 2% of a real assembly
 * -- so it is off unless `-w` asks for it.
 *
 * `-i` is still accepted, so a command line written for the reference runs
 * unaltered, and with the check already off it does what it says. */
bool want_warn = false;

/* The `-w` test lives inside this function rather than at the call site, and
 * the compiler makes it a real call whatever the `inline` keyword says.
 *
 * Hoisting the test into `emit_row` would avoid the call, and costs more than
 * it saves: it takes `assemble_line`'s frame from 108 bytes to 111, and an
 * `ix` displacement is a signed byte, so a frame near 128 is where the hot
 * path starts paying for every access past the edge. A call per immediate is
 * the cheaper of the two. */
void warn_imm(int v, int width) {
    if (want_warn && !fits_imm(v, width)) {
        warn_trunc(v, width);
    }
}

/* Marginal pricing of the label paths. Each duplicates a call to a function
 * that is already out of line, so nothing gets outlined by the measurement and
 * the difference is one extra execution. The data-only flags above are still
 * preferred where one exists for the thing being priced. */
#if defined(DUP_HASH) || defined(DUP_NUMTOK)
volatile int dup_hash_sink;
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

/* The names, in blocks that never move, for the same reason the nodes are.
 *
 * On a machine with 512 KB and no virtual memory, one array grown with realloc
 * is unaffordable: a realloc that has to move holds the old block and the new
 * one at the same time, so an arena of 110 KB needs 228 KB to grow by eight
 * bytes. A block here is never resized and never copied, so the peak is the
 * total.
 *
 * A name is at most 255 characters, so one always fits in a block and there is
 * no oversized case to handle. */
/* Room for `len` characters in the newest block, or a new block.
 *
 * `used` is the offset within the newest one, so the blocks below it are full
 * and are never looked at again -- nothing walks this list except the free at
 * the end, and the local one, which rewinds it. */
char* nam_take(namblock** head, int* used, int len) {
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

bool sym_room(void) {
    if (state.syms_used == SYMS_STEP) {
        Z_SITE("symbol blocks");
        symblock* b = (symblock*) malloc(sizeof(symblock));
        if (b == NULL) {
            return false;
        }
        b->next = state.blocks;
        state.blocks = b;
        state.syms_used = 0;
    }

    return true;
}

/* Case-sensitive, unlike a mnemonic: the reference refuses `jp foo` against a
 * label written FOO. */
static const sym* sym_at(int b, const char* name, int len) {
    for (const sym* sp = state.syms[b].head; sp != NULL; sp = sp->next) {
        if (sp->len != (uint8_t) len) {
            continue;
        }
        /* Walked with pointers rather than indices: the node holds the name as
         * a pointer, so the compare needs no address arithmetic. */
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
sym* sym_intern(const char* name, int len) {
    /* The bucket is passed in rather than computed again. `sym_find` has
     * already hashed the name to know where to search, and hashing is a pass
     * over the name -- the whole cost of the key. */
    DUP_HASH_CALL(name, len);
    const int b = sym_bucket(name, len);

#ifdef DUP_SYMCHAIN
    /* Data only: a decoy ahead of the real entry in the same bucket, same
     * length and differing in the last character, so the compare runs to the
     * end before failing. Doubles the chain walk; the real entry is still
     * found, so the output does not change. */
    char* dtext;
    if (sym_at(b, name, len) == NULL && sym_room()
        && (dtext = nam_take(&state.names, &state.names_used, len)) != NULL) {
        for (int i = 0; i < len; i++) {
            dtext[i] = name[i];
        }
        dtext[len - 1] = (char) (name[len - 1] == 'z' ? 'y' : 'z');
        sym* dec = &state.blocks->nodes[state.syms_used++];
        dec->name = dtext;
        dec->len = (uint8_t) len;
        dec->defined = false;
        dec->islocal = false;
        dec->addr = 0;
        dec->next = state.syms[b].head;
        state.syms[b].head = dec;
    }
#endif

    sym* found = (sym*) sym_at(b, name, len);
    if (found != NULL) {
        return found;
    }
    if (!sym_room()) {
        state.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }

    char* text = nam_take(&state.names, &state.names_used, len);
    if (text == NULL) {
        state.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }
    for (int i = 0; i < len; i++) {
        text[i] = name[i];
    }

    sym* sp = &state.blocks->nodes[state.syms_used++];
    sp->name = text;
    sp->len = (uint8_t) len;
    sp->defined = false;
    /* Set where the node is made, not where it is defined: a global that is
     * referenced before it is defined has to answer this the moment the
     * reference records a fixup against it. */
    sp->islocal = false;
    sp->addr = 0;

    sp->next = state.syms[b].head;
    state.syms[b].head = sp;

    return sp;
}

/* ======================================================================
 * LOCAL LABELS
 *
 * `@name` labels, scoped to the global label above them, plus the
 * anonymous `@@`, `@f` and `@b`. A scope ends at the next global label and
 * its whole table empties in constant time.
 * ====================================================================== */

/* Eight bits of the same key the global table uses. The high three bits it
 * composes are the ones this table does not have room for, so they are simply
 * not asked for; sym_bucket's low byte is the Pearson result itself. */
/* Six bits, taken from the byte the hash pass produces rather than from the
 * composed eleven-bit key. Masking a byte is one instruction; masking an `int`
 * is a call to __iand. */
static inline int loc_bucket(const char* name, int len) {
    return pearson8(name, len) & (NLOCB - 1);
}

/* Room for one more local node and its name. Blocks are threaded once and
 * then reused: after a scope ends loccur walks the same list again. */
static bool loc_room(void) {
    if (state.locs_used == LOCS_STEP || state.loccur == NULL) {
        locblock* next = state.loccur != NULL ? state.loccur->next : state.locfirst;
        if (next == NULL) {
            Z_SITE("local label blocks");
            next = (locblock*) malloc(sizeof(locblock));
            if (next == NULL) {
                return false;
            }
            next->next = NULL;
            if (state.loccur != NULL) {
                state.loccur->next = next;
            } else {
                state.locfirst = next;
            }
        }
        state.loccur = next;
        state.locs_used = 0;
    }

    return true;
}

/* The three folds, settled the way the emitter would have settled them if the
 * label had been behind rather than ahead.
 *
 * Out of line, and out of `patch_fixup`'s own body: almost every fixup in a
 * file is an ordinary address rather than a fold, so one compare sends the
 * rare case here.
 *
 * The range checks are the emitter's, and so are their limits: the reference
 * refuses a bit number above 7 and an interrupt mode above 2 while masking
 * negative ones into range. */
static bool patch_fold(const fixup* f, evalue val, uint8_t w, uint8_t* at) {
    const int v = (int) val;
    if (w == FIX_FOLD_BIT) {
        if (v > 7) {
            state.line = f->line;
            state.err = ZAP_E_INVALID_BIT_NUMBER;

            return false;
        }
        *at |= (uint8_t) ((unsigned) v << 3);

        return true;
    }
    if (w == FIX_FOLD_RST) {
        if (((unsigned) v & ~0x38u) != 0) {
            state.line = f->line;
            state.err = ZAP_E_RESTART_ADDRESS;

            return false;
        }
        *at |= (uint8_t) v;

        return true;
    }

    if (v > 2) {
        state.line = f->line;
        state.err = ZAP_E_INTERRUPT_MODE;

        return false;
    }
    *at |= (uint8_t) ((v == 1 ? 2 : v == 2 ? 3 : 0) << 3);

    return true;
}

/* Patches one reference, now that the address behind it is known. Shared by
 * the end of a scope, which settles that scope's local references, and the end
 * of the source, which settles every global one. */
bool patch_fixup(const fixup* f) {
    const sym* sp = f->target;
    if (!sp->defined) {
        /* Reported against the line that used it, which is long gone; the
         * fixup carries the number for exactly this. The name is still on the
         * symbol, which is the whole reason a reference points at one. */
        state.line = f->line;
        err_tok(sp->name, sp->len);
        state.err = ZAP_E_UNKNOWN_LABEL;

        return false;
    }

    evalue val = sp->addr + f->addend;
    if (f->sub != NULL) {
        if (!f->sub->defined) {
            state.line = f->line;
            err_tok(f->sub->name, f->sub->len);
            state.err = ZAP_E_UNKNOWN_LABEL;

            return false;
        }
        val += (f->width & FIX_SUB2) ? -f->sub->addr : f->sub->addr;
    }

    const uint8_t w = (uint8_t) (f->width & FIX_WIDTH);
    uint8_t* at = state.out + f->off;
    if (w == 0) {
        /* The byte after the displacement byte, which is where a relative
         * jump is measured from. */
        const evalue d = val - (state.org + f->off + 1);
        if (d < -128 || d > 127) {
            state.line = f->line;
            state.err = ZAP_E_RELATIVE_JUMP_TOO_FAR;

            return false;
        }
        *at = (uint8_t) d;

        return true;
    }

    if (w > 4) {
        return patch_fold(f, val, w, at);
    }

    if (want_warn && !fits_width(val, (int) w)) {
        /* Reported against the line that *used* the label rather than the one
         * that defined it; the fixup carries the number for exactly this.
         *
         * Set here rather than at the top of the function, so that the store
         * happens only on the path that needs it. */
        state.line = f->line;
        warn_trunc(val, (int) w);
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
/* Folds the local half of every fixup that names both a global and a local.
 *
 * `end - @loop` goes on the *global* list, because that is where its target
 * belongs, and is patched when the source runs out. By then the node `@loop`
 * points at has been handed back to the allocator and may belong to a later
 * scope's `@loop`, so the subtraction would come out against the wrong address
 * with both halves individually right.
 *
 * The local half is therefore settled here, at the end of the scope where the
 * node still means what it said, and folded into the addend. What is left is
 * an ordinary global fixup. */
bool fold_subs(int from) {
    for (int i = from; i < state.subfix_used; i++) {
        fixup* f = &state.fixups[state.subfix[i]];
        if (f->sub == NULL) {
            continue;
        }
        if (!f->sub->defined) {
            state.line = f->line;
            err_tok(f->sub->name, f->sub->len);
            state.err = ZAP_E_UNKNOWN_LABEL;

            return false;
        }
        const evalue sa = f->sub->addr;
        const evalue folded =
            (evalue) f->addend + ((f->width & FIX_SUB2) ? -sa : sa);
        if (folded < ADDEND_MIN || folded > ADDEND_MAX) {
            /* The local half of a global-minus-local, settled here. It is an
             * address difference in every real case and fits; an EQU wide
             * enough to leave the machine word does not, and says so. */
            state.line = f->line;
            state.err = ZAP_E_CONSTANT_TOO_LARGE_ADD_LABEL;

            return false;
        }
        f->addend = (int) folded;
        f->width &= (uint8_t) ~FIX_SUB2;
        f->sub = NULL;
    }
    state.subfix_used = from;

    return true;
}

bool scope_end(void) {
    if (!fold_subs(0)) {
        return false;
    }
    for (int i = 0; i < state.lfix_used; i++) {
        if (!patch_fixup(&state.lfixups[i])) {
            return false;
        }
    }
    state.lfix_used = 0;
    state.locs_used = LOCS_STEP;   /* forces loc_room back to the first block */
    state.loccur = NULL;
    /* Back to the first block rather than freeing them: a scope ends on every
     * global label, and the blocks are the same size every time. */
    state.locnames = state.locnamfirst;
    state.locnames_used = 0;
    if (++state.gen == 0) {
        /* The stamp has wrapped, so a slot left over from 256 scopes ago would
         * read as belonging to this one. Once every 256 scopes, empty them
         * properly. */
        for (int b = 0; b < NLOCB; b++) {
            state.locs[b].gen = 0;
            state.locs[b].head = NULL;
        }
        state.gen = 1;
    }

    return true;
}

/* Remembers one bucket, so that scope_pop can put it back. Out of line and
 * reached at most once per bucket per expansion. */
__attribute__((noinline))
static bool undo_note(int b) {
    if (state.undo_used == state.undo_cap) {
        /* Grown rather than capped. One entry per distinct bucket per level of
         * nesting is at most 64 times the nesting limit, but a body with sixty
         * local labels is legal -- the reference assembles one -- and a fixed
         * table would refuse it. */
        const int want = state.undo_cap == 0 ? UNDO_STEP : state.undo_cap + state.undo_cap;
        Z_SITE("macro scope");
        locundo* grown = (locundo*) realloc(state.undo, sizeof(locundo) * (size_t) want);
        if (grown == NULL) {
            state.err = ZAP_E_OUT_MEMORY_MACROS;

            return false;
        }
        state.undo = grown;
        state.undo_cap = want;
    }
    locundo* u = &state.undo[state.undo_used++];
    u->head = state.locs[b].head;
    u->b = (uint8_t) b;
    u->gen = state.locs[b].gen;

    return true;
}

/* The entry for a local name in the current scope, made if there is not one.
 *
 * The bucket is empty unless its stamp is this scope's, whatever chain it
 * still holds from an earlier one -- those nodes have been handed back to the
 * allocator and may already be something else. */
sym* loc_intern(const char* name, int len) {
    /* A scope opened by a global label starts here, at the first moment it can
     * matter: nothing but a local reference can tell the difference. Doing it
     * here rather than on every line means only a line with an `@` on it pays.
     *
     * It must be a *later* line than the label, which is the point of the
     * deferral: `two: jp @l` reads its operand in the scope `two` is closing,
     * not the one it opens. */
    if (state.scope_line != 0 && state.scope_line != state.line) {
        state.scope_line = 0;
        if (!scope_end()) {
            return NULL;
        }
    }

    DUP_HASH_CALL(name, len);
    const int b = loc_bucket(name, len);
    if (state.locs[b].gen == state.gen) {
        for (sym* sp = state.locs[b].head; sp != NULL; sp = (sym*) sp->next) {
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
        if (state.expanding != 0 && !undo_note(b)) {
            return NULL;
        }
        state.locs[b].gen = state.gen;
        state.locs[b].head = NULL;
    }

    if (!loc_room()) {
        state.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }

    char* text = nam_take(&state.locnames, &state.locnames_used, len);
    if (text == NULL) {
        state.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }
    if (state.locnamfirst == NULL) {
        state.locnamfirst = state.locnames;
    }
    for (int i = 0; i < len; i++) {
        text[i] = name[i];
    }

    sym* sp = &state.loccur->nodes[state.locs_used++];
    sp->name = text;
    sp->len = (uint8_t) len;
    sp->defined = false;
    sp->islocal = true;
    sp->addr = 0;
    sp->next = state.locs[b].head;
    state.locs[b].head = sp;

    return sp;
}

/* The symbol every `@f` since the last `@@` is waiting on, made if there is
 * not one. Nameless and in no bucket: nothing ever looks it up, and the only
 * thing that finds it again is this field. */
sym* anon_next(void) {
    if (state.anon_fwd == NULL) {
        if (!sym_room()) {
            state.err = ZAP_E_OUT_MEMORY_LABELS;

            return NULL;
        }
        sym* sp = &state.blocks->nodes[state.syms_used++];
        sp->next = NULL;
        sp->name = NULL;
        sp->len = 0;
        sp->defined = false;
        sp->islocal = false;
        sp->addr = 0;
        state.anon_fwd = sp;
    }

    return state.anon_fwd;
}

/* Remembers a reference to a label that is not defined yet. The name is
 * copied for the same reason a definition's is: the line it came from is
 * gone by the time this is resolved. */
/* `addend` is the machine's word and not the evaluator's, which is what keeps
 * the mixed-width compare off the instruction path entirely: an operand's
 * immediate is an `int` and cannot be out of range. The one caller that can
 * hand over something wider is emit_data, and it checks before it calls. */
bool fix_add(const sym* target, const sym* sub, int addend,
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
    /* The line this came from will need its byte columns written again once
     * the label is settled. Set here because this is the one place that knows
     * a line has a forward reference in it, and set unconditionally: the flag
     * is only ever *read* under `listing`, and a store to a fixed address
     * once per forward reference -- 843 of them in isa_real -- is cheaper
     * than the test that would avoid it, never mind moving the option flags
     * above this function to make the test possible. */
    state.fix_touched = true;

    fixup** list = &state.fixups;
    int* used = &state.fix_used;
    int* cap = &state.fix_cap;
    if (target->islocal) {
        list = &state.lfixups;
        used = &state.lfix_used;
        cap = &state.lfix_cap;
    }

    if (*used == *cap) {
        Z_SITE("fixups");
        const int want = *cap + FIX_STEP;
        fixup* grown = (fixup*) realloc(*list, (size_t) want * sizeof(fixup));
        if (grown == NULL) {
            state.err = ZAP_E_OUT_MEMORY_LABELS;

            return false;
        }
        *list = grown;
        *cap = want;
    }

    /* A fixup on the global list whose `sub` is a local has to be settled in
     * two halves; scope_end does the local one. Recorded by index because the
     * list is realloc'd out from under any pointer. */
    if (list == &state.fixups && sub != NULL && sub->islocal) {
        if (state.subfix_used == state.subfix_cap) {
            Z_SITE("fixups");
            const int want = state.subfix_cap == 0 ? 8 : state.subfix_cap + state.subfix_cap;
            int* grown = (int*) realloc(state.subfix, (size_t) want * sizeof(int));
            if (grown == NULL) {
                state.err = ZAP_E_OUT_MEMORY_LABELS;

                return false;
            }
            state.subfix = grown;
            state.subfix_cap = want;
        }
        state.subfix[state.subfix_used++] = *used;
    }

    fixup* f = &(*list)[(*used)++];
    f->target = target;
    f->sub = sub;
    f->addend = addend;
    f->width = width;
    f->off = off;
    f->line = state.line;

    return true;
}
