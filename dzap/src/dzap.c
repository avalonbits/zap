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

#ifdef ZMALLOC
#include "zmalloc.h"
#define Z_SITE(x) (z_site = (x))
#else
#define Z_SITE(x) ((void) 0)
#endif

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
    bool indirect;
    int disp;
    /* The label this operand named, when it is not defined yet. imm holds
     * nothing useful in that case; the emitter records a fixup once it knows
     * where the bytes land, and run patches them all at the end. NULL on every
     * operand that is not a forward reference, which is most of them. */
    const sym* fwd;

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

/* Room for the longest instruction, asked for once.
 *
 * Testing on every byte written meant a bounds check per output byte when an
 * instruction knows in advance that it cannot need more than a handful. The
 * longest form here is two prefixes, an opcode, two displacements and two
 * three-byte immediates. */
#define OUT_MAX_INSN 12

/* ------------------------------------------------------------- symbols */

/* A label, and where it turned out to be.
 *
 * The name is copied. Source lines live in the reader's buffer and are gone
 * as soon as it refills, so a pointer into one is a pointer into the next
 * line by the time a forward reference is resolved.
 */
typedef struct symblock symblock;

struct sym {
    const sym* next;

    /* An offset into the name arena, not a pointer into it.
     *
     * The arena is realloc'd in blocks, and a pointer into it would have to be
     * rebased on every growth -- for every symbol and every fixup. That rebase
     * cannot be tested: glibc extends the block in place, so the pointer does
     * not move, and deleting the rebase failed no check even with six hundred
     * long labels. An offset does not care whether the block moved. */
    int nameoff;
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

    int addr;
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
 * Build with -DDZ_SYMHASH=0 for the other one. Both index the same 2,048
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
#ifndef DZ_SYMHASH
#define DZ_SYMHASH 1
#endif

#if DZ_SYMHASH

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

/* Two passes, composed into eleven bits by writing the bytes rather than
 * shifting: a shift by eight is `call __ishl` on this chip, and the whole
 * point of the comparison is what the key costs. */
static inline int sym_bucket(const char* name, int len) {
    uint8_t h1 = 0;
    uint8_t h2 = 0;
    for (int i = 0; i < len; i++) {
        const uint8_t c = (uint8_t) name[i];
        h1 = pearson[h1 ^ c];
        h2 = pearson[(uint8_t) (h2 ^ c ^ 0x5A)];
    }
    union {
        int v;
        uint8_t b[sizeof(int)];
    } u;
    u.v = 0;
    u.b[0] = h1;
    u.b[1] = (uint8_t) (h2 & 7);

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
 * dzap makes one pass and never looks at a line twice, which is where its
 * speed comes from, so a forward reference cannot be resolved where it is
 * read. The output is held in memory in full, so it is patched at the end
 * instead -- which keeps the single pass and makes the cost of labels a line
 * item that can be measured on its own.
 */
typedef struct {
    const sym* target;  /* interned, so no name and no lookup to do */
    uint8_t width;      /* 1, 2 or 3 bytes, or 0 for a relative displacement */
    int off;            /* where in the output it goes */
    int next_addr;      /* the address after the instruction, for a relative */
    int line;           /* to report against, long after the line is gone */
} fixup;

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
    int cap;

    symslot* syms;      /* NSYMB buckets */
    char* names;        /* arena: every label's text, copied */
    int names_used;
    int names_cap;
    symblock* blocks;   /* symbol nodes, in blocks that never move */
    int syms_used;      /* used in the newest block */
    fixup* fixups;
    int fix_used;
    int fix_cap;

    int line;
    const char* err;

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
    locslot locs[NLOCB];
    locblock* locfirst;     /* kept, to rewind to */
    locblock* loccur;
    int locs_used;          /* in loccur */
    char* locnames;
    int locnames_used;
    int locnames_cap;
    fixup* lfixups;
    int lfix_used;
    int lfix_cap;
    uint8_t gen;            /* which scope the local buckets belong to */
} dz;

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

static bool sym_room(dz* z, int len) {
    if (z->syms_used == SYMS_STEP) {
        Z_SITE("symbol blocks");
        symblock* b = (symblock*) malloc(sizeof(symblock));
        if (b == NULL) {
            return false;
        }
        b->next = z->blocks;
        z->blocks = b;
        z->syms_used = 0;
    }
    if (z->names_used + len > z->names_cap) {
        Z_SITE("label names");
        int want = z->names_cap + NAMES_STEP;
        while (z->names_used + len > want) {
            want += NAMES_STEP;
        }
        char* grown = (char*) realloc(z->names, (size_t) want);
        if (grown == NULL) {
            return false;
        }
        z->names = grown;
        z->names_cap = want;
    }

    return true;
}

/* Case-sensitive, unlike a mnemonic: the reference refuses `jp foo` against a
 * label written FOO. */
static const sym* sym_at(const dz* z, int b, const char* name, int len) {
    for (const sym* sp = z->syms[b].head; sp != NULL; sp = sp->next) {
        if (sp->len != (uint8_t) len) {
            continue;
        }
        const char* text = &z->names[sp->nameoff];
        int i = 0;
        while (i < len && text[i] == name[i]) {
            i++;
        }
        if (i == len) {
            return sp;
        }
    }

    return NULL;
}

/* The entry for a name, made if there is not one. */
static sym* sym_intern(dz* z, const char* name, int len) {
    /* Hashed once. It was hashed twice: sym_find computed the bucket to search
     * and this computed it again to insert, so every name a source mentioned
     * for the first time was walked twice by the hash -- which is the whole
     * cost of a Pearson key. */
    const int b = sym_bucket(name, len);

    sym* found = (sym*) sym_at(z, b, name, len);
    if (found != NULL) {
        return found;
    }
    if (!sym_room(z, len)) {
        z->err = "out of memory for labels";

        return NULL;
    }

    const int off = z->names_used;
    for (int i = 0; i < len; i++) {
        z->names[off + i] = name[i];
    }
    z->names_used += len;

    sym* sp = &z->blocks->nodes[z->syms_used++];
    sp->nameoff = off;
    sp->len = (uint8_t) len;
    sp->defined = false;
    /* Set where the node is made, not where it is defined: a global that is
     * referenced before it is defined has to answer this the moment the
     * reference records a fixup against it. */
    sp->islocal = false;
    sp->addr = 0;

    sp->next = z->syms[b].head;
    z->syms[b].head = sp;

    return sp;
}

/* ------------------------------------------------------------ local labels */

/* Eight bits of the same key the global table uses. The high three bits it
 * composes are the ones this table does not have room for, so they are simply
 * not asked for; sym_bucket's low byte is the Pearson result itself. */
static inline int loc_bucket(const char* name, int len) {
    return sym_bucket(name, len) & (NLOCB - 1);
}

/* Room for one more local node and its name. Blocks are threaded once and
 * then reused: after a scope ends loccur walks the same list again. */
static bool loc_room(dz* z, int len) {
    if (z->locs_used == LOCS_STEP || z->loccur == NULL) {
        locblock* next = z->loccur != NULL ? z->loccur->next : z->locfirst;
        if (next == NULL) {
            Z_SITE("local label blocks");
            next = (locblock*) malloc(sizeof(locblock));
            if (next == NULL) {
                return false;
            }
            next->next = NULL;
            if (z->loccur != NULL) {
                z->loccur->next = next;
            } else {
                z->locfirst = next;
            }
        }
        z->loccur = next;
        z->locs_used = 0;
    }
    if (z->locnames_used + len > z->locnames_cap) {
        Z_SITE("local label names");
        int want = z->locnames_cap + LOCNAMES_STEP;
        while (z->locnames_used + len > want) {
            want += LOCNAMES_STEP;
        }
        char* grown = (char*) realloc(z->locnames, (size_t) want);
        if (grown == NULL) {
            return false;
        }
        z->locnames = grown;
        z->locnames_cap = want;
    }

    return true;
}

/* Patches one reference, now that the address behind it is known. Shared by
 * the end of a scope, which settles that scope's local references, and the end
 * of the source, which settles every global one. */
static bool patch_fixup(dz* z, const fixup* f) {
    const sym* sp = f->target;
    if (!sp->defined) {
        /* Reported against the line that used it, which is long gone; the
         * fixup carries the number for exactly this. */
        z->line = f->line;
        z->err = "unknown label";

        return false;
    }

    uint8_t* at = z->out + f->off;
    if (f->width == 0) {
        const int d = sp->addr - f->next_addr;
        if (d < -128 || d > 127) {
            z->line = f->line;
            z->err = "relative jump too far";

            return false;
        }
        *at = (uint8_t) d;

        return true;
    }

    at[0] = (uint8_t) sp->addr;
    if (f->width > 1) {
        at[1] = (uint8_t) (sp->addr >> 8);
    }
    if (f->width > 2) {
        at[2] = (uint8_t) (sp->addr >> 16);
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
static bool scope_end(dz* z) {
    for (int i = 0; i < z->lfix_used; i++) {
        if (!patch_fixup(z, &z->lfixups[i])) {
            return false;
        }
    }
    z->lfix_used = 0;
    z->locs_used = LOCS_STEP;   /* forces loc_room back to the first block */
    z->loccur = NULL;
    z->locnames_used = 0;
    if (++z->gen == 0) {
        /* The stamp has wrapped, so a slot left over from 256 scopes ago would
         * read as belonging to this one. Once every 256 scopes, empty them
         * properly. */
        for (int b = 0; b < NLOCB; b++) {
            z->locs[b].gen = 0;
            z->locs[b].head = NULL;
        }
        z->gen = 1;
    }

    return true;
}

/* The entry for a local name in the current scope, made if there is not one.
 *
 * The bucket is empty unless its stamp is this scope's, whatever chain it
 * still holds from an earlier one -- those nodes have been handed back to the
 * allocator and may already be something else. */
static sym* loc_intern(dz* z, const char* name, int len) {
    /* A scope a global label opened has to have started before this, and this
     * is the first moment it can matter: nothing but a local can tell the
     * difference. Asked here rather than on every line of the source, where it
     * cost 3.5% to answer a question only a line with an `@` on it can ask.
     *
     * On a later line than the label, though, and that is the whole point of
     * the deferral -- `two: jp @l` reads its operand in the scope `two` is
     * closing, not the one it opens, so the line the label was on is what has
     * to be compared and not merely whether there was one. */
    if (z->scope_line != 0 && z->scope_line != z->line) {
        z->scope_line = 0;
        if (!scope_end(z)) {
            return NULL;
        }
    }

    const int b = loc_bucket(name, len);
    if (z->locs[b].gen == z->gen) {
        for (sym* sp = z->locs[b].head; sp != NULL; sp = (sym*) sp->next) {
            if (sp->len != (uint8_t) len) {
                continue;
            }
            const char* t = &z->locnames[sp->nameoff];
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
        z->locs[b].gen = z->gen;
        z->locs[b].head = NULL;
    }

    if (!loc_room(z, len)) {
        z->err = "out of memory for labels";

        return NULL;
    }

    const int off = z->locnames_used;
    for (int i = 0; i < len; i++) {
        z->locnames[off + i] = name[i];
    }
    z->locnames_used += len;

    sym* sp = &z->loccur->nodes[z->locs_used++];
    sp->nameoff = off;
    sp->len = (uint8_t) len;
    sp->defined = false;
    sp->islocal = true;
    sp->addr = 0;
    sp->next = z->locs[b].head;
    z->locs[b].head = sp;

    return sp;
}

static bool sym_define(dz* z, const char* name, int len, int addr) {
    sym* sp = sym_intern(z, name, len);
    if (sp == NULL) {
        return false;
    }
    if (sp->defined) {
        z->err = "label defined twice";

        return false;
    }
    sp->defined = true;
    sp->addr = addr;

    return true;
}

/* Defines a local in the current scope. Same shape as sym_define, against the
 * other table. */
static bool loc_define(dz* z, const char* name, int len, int addr) {
    sym* sp = loc_intern(z, name, len);
    if (sp == NULL) {
        return false;
    }
    if (sp->defined) {
        z->err = "label defined twice";

        return false;
    }
    sp->defined = true;
    sp->addr = addr;

    return true;
}

/* Remembers a reference to a label that is not defined yet. The name is
 * copied for the same reason a definition's is: the line it came from is
 * gone by the time this is resolved. */
static bool fix_add(dz* z, const sym* target, uint8_t width, int off,
                    int next_addr) {
    /* A reference to a local goes on the scope's own list, because the node it
     * points at stops meaning this label the moment the scope ends. The flag
     * is on the node rather than on the operand that carried it here: the
     * operand is copied twice a line with an ldir and this is written once per
     * distinct label. */
    fixup** list = &z->fixups;
    int* used = &z->fix_used;
    int* cap = &z->fix_cap;
    if (target->islocal) {
        list = &z->lfixups;
        used = &z->lfix_used;
        cap = &z->lfix_cap;
    }

    if (*used == *cap) {
        Z_SITE("fixups");
        const int want = *cap + FIX_STEP;
        fixup* grown = (fixup*) realloc(*list, (size_t) want * sizeof(fixup));
        if (grown == NULL) {
            z->err = "out of memory for labels";

            return false;
        }
        *list = grown;
        *cap = want;
    }

    fixup* f = &(*list)[(*used)++];
    f->target = target;
    f->width = width;
    f->off = off;
    f->next_addr = next_addr;
    f->line = z->line;

    return true;
}

/* ---------------------------------------------------------------- output */

static bool out_grow(dz* z) {
        Z_SITE("output buffer");
    const int want = z->cap + OUT_STEP;
    uint8_t* grown = (uint8_t*) realloc(z->out, (size_t) want);
    if (grown == NULL) {
        return false;
    }

    /* realloc is allowed to move the buffer, so the cursor and the limit are
     * both relative to a base that may no longer be there. */
    z->o = grown + (z->o - z->out);
    z->out = grown;
    z->cap = want;
    z->lim = grown + want - OUT_MAX_INSN;

    return true;
}

/* One `if`, not a loop: OUT_STEP is 32 KB and an instruction is at most 12
 * bytes, so one growth always leaves room. */
static bool out_reserve(dz* z) {
    if (z->o <= z->lim) {
        return true;
    }

    return out_grow(z);
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
#if DZ_SYMHASH
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
    for (int i = 1; i < n; i++) {
        if (name[i] != (s[i] | 0x20)) {
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
    .indirect = false,
    .disp = 0,
    .fwd = NULL,
    .has_imm = false,
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
 * the low byte first for the same reason. Digits past the third byte are
 * dropped, which is what the old accumulator did too once it overflowed.
 *
 * Taking the run rather than the whole token is what lets `0x1234` and
 * `1234h` share this. They used to be different code: the prefixed form came
 * here and the suffixed form fell through to num_parse, which on the honest
 * corpus is 46% of every immediate in isa_real. */
static inline bool hex_digits(const char* d, int n, int* out) {
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

    /* Digits past the third byte are dropped from the value but must still be
     * rejected if they are not hex, or a literal the reference refuses would
     * assemble here. Only a literal of more than six digits reaches this. */
    while (j > 0) {
        bad |= hexval[(uint8_t) d[--j]];
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

    int v = 0;
    if (n >= 3 && s[0] == '0' && (s[1] | 0x20) == 'x') {
        return hex_digits(s + 2, n - 2, &v);
    }
    if (n >= 2 && (s[n - 1] | 0x20) == 'h') {
        return hex_digits(s, n - 1, &v);
    }
    value gv = 0;

    return num_parse(s, n, &gv);
}

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
__attribute__((always_inline)) static inline bool parse_operand(dz* z, dop* op, const char** pp, const char* e) {
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
    const char* known_end = NULL;
    if ((cl & C_ALPHA) != 0) {
        const char* s = p;
        while (name_ch(*p)) {
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
            while (is_space_ch(*p)) {
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

        /* Not a register -- but it can still be a literal.
         *
         * A hexadecimal constant written with a trailing h begins with one of
         * a..f, so `ld hl, aabbcch` arrives here looking exactly like a name.
         * num_parse has always known the suffix forms; the operand simply
         * never reached it, and this said "unknown operand" instead. Forty
         * forms of the reference's own corpus were wrong for as long as that
         * was true, and the corpus could not say so because it was filtered
         * through dzap.
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

    /* A literal. */
    {
        const char* s = p;
        if (known_end != NULL) {
            /* Already scanned, by the register path that rewound to here. */
            p = known_end;
        } else {
            if (*p == '-' || *p == '+') {
                p++;
            }
            /* Bounded, for the reason given at the displacement scan above. */
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
        int v = 0;
        bool got = false;
        if (ns[0] == '@') {
            /* `@f` and `@n` are the next anonymous label, `@b` and `@p` the
             * previous one -- reserved spellings in the reference, whatever a
             * local of that name would mean, and a local really can be called
             * `@b`: the reference accepts the definition and then leaves it
             * unreachable. Refused rather than read as a local, which would
             * resolve to the wrong address in a source that has both. */
            if (nn == 2) {
                const char k = (char) (ns[1] | 0x20);
                if (k == 'f' || k == 'n' || k == 'b' || k == 'p') {
                    z->err = "anonymous labels are not supported";

                    return false;
                }
            }
            /* A local label, and nothing else: no radix accepts a leading at
             * sign, and the reference does not test a local against the number
             * formats either. Going straight to the lookup also keeps `@abch`
             * from being read as hexadecimal by the trailing-h rule below. */
            const sym* sp = loc_intern(z, ns, nn);
            if (sp == NULL) {
                return false;
            }
            if (sp->defined) {
                v = sp->addr;
            } else if (neg) {
                z->err = "a label cannot be negated";

                return false;
            } else {
                op->fwd = sp;
                v = 0;
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
            const sym* sp = sym_intern(z, ns, nn);
            if (sp == NULL) {
                return false;
            }
            if (sp->defined) {
                v = sp->addr;
                got = true;
            } else if (neg) {
                /* -label would have to negate an address that is not known
                 * yet. The reference has expressions; dzap does not, and
                 * refusing is better than emitting the address unnegated. */
                z->err = "a label cannot be negated";

                return false;
            } else {
                op->fwd = sp;
                v = 0;
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
            if ((row->cpu & CPU_EZ80) == 0) {
                return NULL;
            }

            return row;
        }
    }

    return NULL;
}

static const isa_row* match_row(const insninfo* insn,
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
            if (op->has_imm) {
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
            op->has_imm = false;
            break;
        case TR_BIT:
            out->opcode |= shl3[imm_lo(op) & 7];
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

static uint8_t* emit_imm(uint8_t* o, const dop* op, uint8_t cond) {
    const int width = (cond & IMM_N) ? 1 : (DZ_ADL ? 3 : 2);

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

__attribute__((always_inline)) static inline bool emit_row(dz* z, const isa_row* row, dop* a, dop* b) {
    if (!out_reserve(z)) {
        return false;
    }

    /* One cursor for the whole instruction rather than z->out[z->pos++] per
     * byte. put() reloaded both the output base and the position, added them,
     * stored the byte and stored the position back, for every byte written --
     * twenty-three loads of those two fields in this function alone. The
     * reservation above is what makes a bare cursor safe: room for the
     * longest form is already there, so nothing between here and the
     * write-back can move the buffer. */
    uint8_t* o = z->o;

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
        *o++ = out.prefix1;
    }
    if (out.prefix2 != 0) {
        *o++ = out.prefix2;
    }
    if (!dd_before_opcode) {
        *o++ = out.opcode;
    }
    if (row->flags & F_DISPA) {
        *o++ = (uint8_t) (a->disp & 0xFF);
    }
    if (row->flags & F_DISPB) {
        *o++ = (uint8_t) (b->disp & 0xFF);
    }
    if (dd_before_opcode) {
        *o++ = out.opcode;
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
        if (rel->fwd != NULL) {
            /* Forward: the displacement is not known, so a zero goes down and
             * the fixup carries the address it will be measured from. Whether
             * it is in reach is decided when it is patched. */
            if (!fix_add(z, rel->fwd, 0, (int) (o - z->out),
                         DZ_ORG + (int) (o - z->out) + 1)) {
                return false;
            }
            *o++ = 0;
        } else {
            const int d = rel->imm - (DZ_ORG + (int) (o - z->out) + 1);
            if (d < -128 || d > 127) {
                z->err = "relative jump too far";

                return false;
            }
            *o++ = (uint8_t) d;
        }
    } else {
        if (a->has_imm && (row->condA & (IMM_N | IMM_MMN))) {
            if (a->fwd != NULL
                && !fix_add(z, a->fwd,
                            (row->condA & IMM_N) ? 1 : (DZ_ADL ? 3 : 2),
                            (int) (o - z->out), 0)) {
                return false;
            }
            o = emit_imm(o, a, row->condA);
        }
        if (b->has_imm && (row->condB & (IMM_N | IMM_MMN))) {
            if (b->fwd != NULL
                && !fix_add(z, b->fwd,
                            (row->condB & IMM_N) ? 1 : (DZ_ADL ? 3 : 2),
                            (int) (o - z->out), 0)) {
                return false;
            }
            o = emit_imm(o, b, row->condB);
        }
    }

    z->o = o;

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
/* Whether a token would be read as a number rather than as a name.
 *
 * A label cannot be spelled like a literal: the reference refuses `a00h:`,
 * `ffh:`, `e5h:`, `ah:` and `1010b:` -- all of which are numbers with a radix
 * suffix -- while accepting `beef:`, `zzh:`, `h:` and `a0h_x:`, none of which
 * are. dzap accepted every one of them, which is a divergence in the direction
 * that produces plausible bytes rather than an error: `ffh: nop` defined a
 * label the reference would have refused, and any later `ld a, ffh` then meant
 * something different in the two assemblers.
 *
 * Found by a benchmark generator that produced `a00h` by accident.
 *
 * The same tests the operand parser uses, in the same order, so the two cannot
 * disagree about what a number is. */

__attribute__((noinline)) static bool assemble_line(dz* z, const char* p, const char* e, const char** stop) {
    /* Bounded, and it has to be.
     *
     * Unbounded, this compiles to a loop rotated wrongly -- the pointer is
     * pre-decremented and each turn tests one character past it, so the first
     * is never examined and the scan stops one short. It is the same fault the
     * num_ch scans carry a bound for, and it does not reduce: the loop was
     * correct until labels were added around it, and nothing about labels
     * touches it. The bound is what stops the rotation.
     *
     * Every line starts here, so this is worth re-checking in the generated
     * assembly whenever the function changes. `dec iy` before the loop head is
     * the tell. */
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
    while ((cclass[(uint8_t) *p] & C_MNEM) != 0) {
        p++;
    }
    int n = (int) (p - s);
    if (n == 0) {
        z->err = "expected an instruction";

        return false;
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
        const int addr = DZ_ORG + (int) (z->o - z->out);
        if (*s == '@') {
            /* `@@` is the reference's anonymous label, not a local: it may be
             * defined any number of times and is reached by `@f` and `@b`
             * rather than by name. dzap has no such thing, and treating it as
             * an ordinary local would accept the first one and then call the
             * second a redefinition -- a wrong answer wearing a right-looking
             * error. Refused by name instead. 171 of them in the Agon corpus,
             * so this is a feature to add and not an oddity to ignore. */
            if (n == 2 && s[1] == '@') {
                z->err = "anonymous labels are not supported";

                return false;
            }
            /* A local. It is not tested against the number formats: the
             * reference returns before that check for a local, so `@123:` and
             * `@0ffh:` are labels there and have to be here. */
            if (!loc_define(z, s, n, addr)) {
                return false;
            }
        } else {
            if (numeric_token(s, n)) {
                z->err = "invalid label";

                return false;
            }
            if (!sym_define(z, s, n, addr)) {
                return false;
            }
            /* A global ends the scope before it starts a new one -- but not
             * until this line is done with, because the rest of it still
             * belongs to the scope being closed. */
            z->scope_line = z->line;
        }
        p++;
        while (is_space_ch(*p)) {
            p++;
        }
        *stop = p;
        if (p >= e || *p == '\n' || *p == ';') {
            return true;
        }

        s = p;
        while ((cclass[(uint8_t) *p] & C_MNEM) != 0) {
            p++;
        }
        n = (int) (p - s);
        if (n == 0) {
            z->err = "expected an instruction";

            return false;
        }
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

/* Patches every forward reference, once the whole source has been read.
 *
 * This is what buys the single pass. dzap never looks at a line twice, so a
 * reference to a label defined later cannot be resolved where it is read; the
 * output is held in memory in full, so it is patched here instead. The cost of
 * labels is then a line item -- this loop and the table behind it -- rather
 * than a second pass over the source, which would be most of the program
 * again.
 *
 * Little-endian, as everywhere else here. */
static bool resolve_fixups(dz* z) {
    for (int i = 0; i < z->fix_used; i++) {
        if (!patch_fixup(z, &z->fixups[i])) {
            return false;
        }
    }

    return true;
}

static bool run(dz* z, const char* path) {
    Z_SITE("source reader");
    if (br_open(&z->rd, path, BUF_KB) == NULL) {
        z->err = "cannot open source";

        return false;
    }

    z->cap = (int) (z->rd.fsz_ >> OUT_SHIFT);
    if (z->cap < OUT_MIN) {
        z->cap = OUT_MIN;
    }
    Z_SITE("output buffer");
    z->out = (uint8_t*) malloc((size_t) z->cap);
    if (z->out == NULL) {
        z->err = "out of memory";

        return false;
    }
    z->o = z->out;
    z->lim = z->out + z->cap - OUT_MAX_INSN;
    Z_SITE("symbol buckets");
    z->syms = (symslot*) calloc(NSYMB, sizeof(symslot));
    if (z->syms == NULL) {
        z->err = "out of memory";

        return false;
    }
    /* One block up front, so sym_define never has to ask whether there is
     * one; it only ever asks whether the newest is full. */
    Z_SITE("symbol blocks");
    z->blocks = (symblock*) malloc(sizeof(symblock));
    if (z->blocks == NULL) {
        z->err = "out of memory";

        return false;
    }
    z->blocks->next = NULL;
    z->syms_used = 0;
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
            while (is_space_ch(*stop)) {
                stop++;
            }
            if (*stop == ';') {
                /* A remark after the instruction. Its body is never looked at
                 * -- the search for the newline below walks it once and that
                 * is all a comment ever costs. */
                while (*stop != '\n') {
                    stop++;
                }
            } else if (*stop != '\n') {
                z->err = "unexpected text after the instruction";

                return false;
            }
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

    /* The last scope ends with the source, and settles the same way any other
     * one does. Before the globals, because a local that was never defined
     * should be reported against the line that used it rather than after a
     * global's failure somewhere else. */
    if (!scope_end(z)) {
        return false;
    }

    return resolve_fixups(z);
}

/* Everything run() may have allocated.
 *
 * Written once because it was written three times: the two error paths and
 * the success path each freed their own list, and the block list was added to
 * one of them. The sanitiser found it; a machine with 512 KB and no leak
 * checker would have found it later and less clearly. */
static void dz_free(dz* z) {
    free(z->out);
    free(z->syms);
    free(z->names);
    free(z->fixups);
    free(z->locnames);
    free(z->lfixups);
    while (z->blocks != NULL) {
        symblock* next = z->blocks->next;
        free(z->blocks);
        z->blocks = next;
    }
    while (z->locfirst != NULL) {
        locblock* next = z->locfirst->next;
        free(z->locfirst);
        z->locfirst = next;
    }
    br_destroy(&z->rd);
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
        dz_free(&z);

        return 1;
    }

    const uint8_t fh = mos_fopen(argv[2], FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        printf("Cannot write %s\r\n", argv[2]);
        dz_free(&z);

        return 1;
    }
    const int written = (int) (z.o - z.out);
    if (written > 0) {
        mos_fwrite(fh, (char*) z.out, (uint24_t) written);
    }
    mos_fclose(fh);

    printf("Wrote %s, %d bytes\r\n", argv[2], written);

#ifdef ZMALLOC
    z_report();
#endif

    const uint24_t cs = elapsed_cs(begin, end);
    printf("Done in %u.%02u seconds\r\n", (unsigned) (cs / 100),
           (unsigned) (cs % 100));

    dz_free(&z);

    return 0;
}
