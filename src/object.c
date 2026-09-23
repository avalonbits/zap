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
#include "scan.h"
#include "symtab.h"

/* ======================================================================
 * OBJECTS
 *
 * What `-f` changes. Everything here is reached only when an object is being
 * written, so a flat assembly pays for none of it beyond the few tests of
 * `obj_format` that lead here. See docs/LIBRARIES.md for the design.
 *
 * The segments are held in memory whole, because an object is written only
 * once every relocation is known. The segment being assembled into is the
 * output window -- `state.win`, `o`, `lim` and `wbase` -- so the emitters
 * cannot tell an object from a flat binary; switching segments swaps the
 * window, and a full window grows rather than being written out.
 * ====================================================================== */

uint8_t obj_format;

typedef struct {
    uint8_t* buf;
    int len;
    int cap;
    int pend;       /* reserved and not written: DS and ALIGN, as in `state` */
    int align;      /* the largest ALIGN, which the segment is placed on */
} objseg;

static objseg segs[SEG_COUNT];

/* The segment in the window, or 0 once the assembly is over and every
 * segment is back in `segs`. */
static uint8_t cur;

/* One symbol per segment, standing for its first byte.
 *
 * `$` and `@b` are positions rather than labels, and a position is its
 * segment plus an offset: these are the segment half. A local placed in a
 * segment becomes one of these too, where it has to outlive its scope. */
static sym secsym[SEG_COUNT];

static const char* const segname[SEG_COUNT] = {
    NULL, ".text", ".data", ".bss", ".rodata"
};

/* The window while the bss is being assembled into. The bss holds no bytes,
 * so the window is set up with no room at all: `o` past `lim`, which sends any
 * write to obj_grow and its refusal, while DS and ALIGN only add to `pend`.
 * `wbase` is one short of the segment so that `out_here()` still counts from
 * its start. */
static uint8_t bss_window[2];

#define OBJ_SEG_START 4096

/* A relocation: the linker adds the address of `base` and `addend` into the
 * field at `off`, a position with its segment in the upper bits. */
typedef struct {
    int off;
    const sym* base;
    evalue addend;
    uint8_t type;
} objreloc;

static objreloc* relocs;
static int reloc_used;
static int reloc_cap;

/* The labels XDEF and XREF named, in the order they were named, and the line
 * each was named on, to report an export that was never defined against. */
typedef struct {
    sym* sp;
    int line;
} objlink;

static objlink* links;
static int link_used;
static int link_cap;

#define R_Z80_24 5

static void seg_enter(uint8_t k) {
    cur = k;
    state.pend = segs[k].pend;
    if (k == SEG_BSS) {
        state.win = bss_window;
        state.o = bss_window + 1;
        state.lim = bss_window;
        state.wbase = (k << SEG_SHIFT) - 1;

        return;
    }
    state.win = segs[k].buf;
    state.o = state.win + segs[k].len;
    state.cap = segs[k].cap;
    state.lim = state.win + state.cap - OUT_MAX_INSN;
    state.wbase = k << SEG_SHIFT;
}

static bool seg_save(void) {
    if (cur == 0) {
        return true;
    }
    segs[cur].pend = state.pend;
    if (cur != SEG_BSS) {
        segs[cur].buf = state.win;
        segs[cur].len = (int) (state.o - state.win);
        segs[cur].cap = state.cap;
    }
    if (out_here() - (cur << SEG_SHIFT) > SEG_MAX) {
        state.err = ZAP_E_OBJ_SEGMENT_TOO_LARGE;

        return false;
    }

    return true;
}

static bool seg_alloc(uint8_t k) {
    if (k == SEG_BSS || segs[k].buf != NULL) {
        return true;
    }
    Z_SITE("object segment");
    segs[k].buf = (uint8_t*) malloc(OBJ_SEG_START);
    if (segs[k].buf == NULL) {
        state.err = ZAP_E_OUT_MEMORY_OUTPUT;

        return false;
    }
    segs[k].cap = OBJ_SEG_START;

    return true;
}

/* Sets up the segments in place of the flat output window. Labels before the
 * first SEGMENT are in CODE. */
bool obj_start(void) {
    for (int k = 0; k < SEG_COUNT; k++) {
        segs[k].buf = NULL;
        segs[k].len = 0;
        segs[k].cap = 0;
        segs[k].pend = 0;
        segs[k].align = 1;

        sym* sp = &secsym[k];
        sp->next = NULL;
        sp->name = segname[k];
        sp->len = 0;
        sp->defined = false;
        sp->islocal = false;
        sp->reloc = k != 0 ? SYM_PLACED : 0;
        sp->addr = k << SEG_SHIFT;
    }
    cur = 0;
    reloc_used = 0;
    link_used = 0;
    if (!seg_alloc(SEG_CODE)) {
        return false;
    }
    seg_enter(SEG_CODE);

    return true;
}

/* What out_flush does in an object: the window is full, and grows. */
bool obj_grow(void) {
    if (cur == SEG_BSS) {
        state.err = ZAP_E_OBJ_BSS_HOLDS_NO_BYTES;

        return false;
    }
    const int len = (int) (state.o - state.win);
    if (len > SEG_MAX) {
        state.err = ZAP_E_OBJ_SEGMENT_TOO_LARGE;

        return false;
    }
    const int want = state.cap + state.cap;
    Z_SITE("object segment");
    uint8_t* grown = (uint8_t*) realloc(state.win, (size_t) want);
    if (grown == NULL) {
        state.err = ZAP_E_OUT_MEMORY_OUTPUT;

        return false;
    }
    state.win = grown;
    state.o = grown + len;
    state.cap = want;
    state.lim = grown + want - OUT_MAX_INSN;

    return true;
}

/* Switches the segment being assembled into.
 *
 * Space reserved at the end of a segment is written before leaving it, except
 * in the bss, where reserving is all there is. A flat binary drops a
 * reservation at its very end, but in an object that space belongs to a
 * variable. */
static bool seg_switch(uint8_t k) {
    if (k == cur) {
        return true;
    }
    if (cur != SEG_BSS && state.pend != 0 && !out_settle()) {
        return false;
    }
    if (!seg_save() || !seg_alloc(k)) {
        return false;
    }
    seg_enter(k);

    return true;
}

/* ALIGN in an object: aligned relative to the segment, which the linker then
 * places on a boundary at least this large. */
bool obj_align(evalue n) {
    if (n > (evalue) OBJ_ALIGN_MAX) {
        state.err = ZAP_E_OBJ_ALIGN_TOO_LARGE;

        return false;
    }
    if (n > (evalue) segs[cur].align) {
        segs[cur].align = (int) n;
    }

    return true;
}

/* The end of the assembly: the segment in the window goes back with the
 * others, so that every fixup finds its site the same way. */
bool obj_finish(void) {
    if (cur != SEG_BSS && state.pend != 0 && !out_settle()) {
        return false;
    }
    /* Every export has to be something: a label placed here, or a number. */
    for (int i = 0; i < link_used; i++) {
        const sym* sp = links[i].sp;
        if ((sp->reloc & SYM_XDEF) != 0 && !sp->defined
            && (sp->reloc & SYM_PLACED) == 0) {
            state.line = links[i].line;
            err_tok(sp->name, sp->len);
            state.err = ZAP_E_OBJ_XDEF_UNDEFINED;

            return false;
        }
    }
    if (!seg_save()) {
        return false;
    }
    cur = 0;
    state.win = NULL;
    state.o = NULL;
    state.lim = NULL;
    state.wbase = 0;
    state.pend = 0;

    return true;
}

void obj_free(void) {
    seg_save();
    for (int k = 1; k < SEG_COUNT; k++) {
        free(segs[k].buf);
        segs[k].buf = NULL;
    }
    free(relocs);
    relocs = NULL;
    reloc_used = 0;
    reloc_cap = 0;
    free(links);
    links = NULL;
    link_used = 0;
    link_cap = 0;
    cur = 0;
    state.win = NULL;
}

/* Where a position is in memory, whichever segment it is in. */
uint8_t* obj_ptr(int off) {
    const int k = off >> SEG_SHIFT;
    if (k == cur) {
        return state.win + (off - state.wbase);
    }

    return segs[k].buf + (off & SEG_MAX);
}

const sym* obj_section(int addr) {
    return &secsym[addr >> SEG_SHIFT];
}


static bool reloc_add(int off, const sym* base, evalue addend, uint8_t type) {
    if (reloc_used == reloc_cap) {
        Z_SITE("relocations");
        const int want = reloc_cap == 0 ? 64 : reloc_cap + reloc_cap;
        objreloc* grown = (objreloc*) realloc(relocs, (size_t) want * sizeof(objreloc));
        if (grown == NULL) {
            state.err = ZAP_E_OUT_MEMORY_LABELS;

            return false;
        }
        relocs = grown;
        reloc_cap = want;
    }
    objreloc* r = &relocs[reloc_used++];
    r->off = off;
    r->base = base;
    r->addend = addend;
    r->type = type;

    return true;
}

/* What an address is relative to: its segment for a label placed here, the
 * label itself for one imported, and nothing for a number. */
static const sym* base_of(const sym* sp) {
    if ((sp->reloc & SYM_PLACED) != 0) {
        return obj_section((int) sp->addr);
    }
    if ((sp->reloc & SYM_XREF) != 0) {
        return sp;
    }

    return NULL;
}

/* A fixup's value in an object, and whether it can be written as a number.
 *
 * An address counts once, and one subtracted counts minus once. Where the
 * count comes to nothing the value is a plain number -- a constant, or the
 * distance between two labels in the same segment, where the segments cancel.
 * A relative jump to a label in its own segment is a distance too.
 *
 * An address counted once in a 24-bit field becomes a relocation, and the
 * field is written as zeros: the linker adds the addend it carries. Anything
 * else needs a relocation zap does not write yet, or is not something an
 * object can express at all. */
bool obj_value(const fixup* f, evalue* val) {
    const sym* t = f->target;
    const sym* s = f->sub;
    if (!t->defined && (t->reloc & SYM_LINKED) == 0) {
        state.line = f->line;
        err_tok(t->name, t->len);
        state.err = ZAP_E_UNKNOWN_LABEL;

        return false;
    }
    if (s != NULL && !s->defined && (s->reloc & SYM_LINKED) == 0) {
        state.line = f->line;
        err_tok(s->name, s->len);
        state.err = ZAP_E_UNKNOWN_LABEL;

        return false;
    }

    evalue v = t->addr + f->addend;
    const sym* base = base_of(t);
    int net = base != NULL;
    if (s != NULL) {
        const bool minus = (f->width & FIX_SUB2) != 0;
        v += minus ? -s->addr : s->addr;
        const sym* sb = base_of(s);
        if (sb != NULL) {
            if (minus && base == sb) {
                net = 0;
                base = NULL;
            } else if (!minus && base == NULL) {
                net = 1;
                base = sb;
            } else {
                net = 2;
            }
        }
    }
    *val = v;

    const uint8_t w = (uint8_t) (f->width & FIX_WIDTH);
    if (w == FIX_DSINIT) {
        return true;
    }
    if (w == 0) {
        if (net == 1 && base == obj_section(f->off)) {
            return true;
        }
        state.line = f->line;
        state.err = net == 2 ? ZAP_E_OBJ_NOT_RELOCATABLE : ZAP_E_OBJ_NO_RELOCATIONS_YET;

        return false;
    }
    if (net == 0) {
        return true;
    }
    if (net == 1 && w == 3) {
        /* The addend is what is left once the base's own address is taken
         * out: a segment's offset, or whatever was added to an import. */
        if ((base->reloc & SYM_XREF) != 0) {
            ((sym*) base)->reloc |= SYM_USED;
        }
        *val = 0;

        return reloc_add(f->off, base, v - base->addr, R_Z80_24);
    }
    state.line = f->line;
    state.err = (net == 1 && w <= 2) ? ZAP_E_OBJ_NO_RELOCATIONS_YET
                                     : ZAP_E_OBJ_NOT_RELOCATABLE;

    return false;
}

/* XDEF and XREF, with the GNU spellings: a list of names, each exported or
 * imported. ZDS writes `XREF name:ROM` to say where an import lives, which
 * means nothing here, so the suffix is read and dropped. */
static bool link_names(const char* p, const char* e, const char** stop,
                       uint8_t flag) {
    while (true) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        const char* name = p;
        while (p < e && name_ch(*p)) {
            p++;
        }
        const int n = (int) (p - name);
        if (n == 0) {
            state.err = ZAP_E_EXPECTED_LABEL_NAME;

            return false;
        }
        if (*name == '@') {
            err_tok(name, n);
            state.err = ZAP_E_OBJ_LOCAL_LINKED;

            return false;
        }
        if ((unsigned) n > LABEL_MAX) {
            state.err = ZAP_E_LABEL_TOO_LONG;

            return false;
        }
        if (flag == SYM_XREF && p < e && *p == ':') {
            p++;
            while (p < e && name_ch(*p)) {
                p++;
            }
        }
        sym* sp = sym_intern(name, n);
        if (sp == NULL) {
            return false;
        }
        const uint8_t other = flag == SYM_XREF ? SYM_XDEF : SYM_XREF;
        if ((sp->reloc & other) != 0) {
            err_tok(name, n);
            state.err = ZAP_E_OBJ_XDEF_AND_XREF;

            return false;
        }
        if (flag == SYM_XREF && (sp->defined || (sp->reloc & SYM_PLACED) != 0)) {
            err_tok(name, n);
            state.err = ZAP_E_OBJ_XREF_DEFINED;

            return false;
        }
        if ((sp->reloc & flag) == 0) {
            sp->reloc |= flag;
            if (link_used == link_cap) {
                Z_SITE("exports");
                const int want = link_cap == 0 ? 16 : link_cap + link_cap;
                objlink* grown = (objlink*) realloc(links, (size_t) want * sizeof(objlink));
                if (grown == NULL) {
                    state.err = ZAP_E_OUT_MEMORY_LABELS;

                    return false;
                }
                links = grown;
                link_cap = want;
            }
            links[link_used].sp = sp;
            links[link_used].line = state.line;
            link_used++;
        }
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (p >= e || *p != ',') {
            break;
        }
        p++;
    }
    *stop = p;

    return true;
}

/* SEGMENT and the GNU spellings of it, which exist only in an object.
 *
 * `*mine` says whether this was one of them at all; a line that is not goes
 * on to be a macro or an unknown instruction, as it would in a flat
 * assembly. */
static const char* seg_token(const char* p, const char* e, const char** end) {
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    const char* s = p;
    if (p < e && *p == '.') {
        p++;
    }
    while (p < e && name_ch(*p)) {
        p++;
    }
    *end = p;

    return s;
}

static bool same_word(const char* s, int n, const char* want) {
    int i = 0;
    for (; i < n; i++) {
        if (want[i] == 0 || (s[i] | 0x20) != want[i]) {
            return false;
        }
    }

    return want[i] == 0;
}

/* A segment by its ZDS name, or by its GNU section name. 0 if neither. */
static uint8_t seg_of(const char* s, int n) {
    if (same_word(s, n, "code") || same_word(s, n, ".text")) {
        return SEG_CODE;
    }
    if (same_word(s, n, "data") || same_word(s, n, ".data")) {
        return SEG_DATA;
    }
    if (same_word(s, n, "bss") || same_word(s, n, ".bss")) {
        return SEG_BSS;
    }
    if (same_word(s, n, "rodata") || same_word(s, n, ".rodata")) {
        return SEG_RODATA;
    }

    return 0;
}

bool obj_directive(const char* s, int n, const char* p, const char* e,
                   const char** stop, bool* mine) {
    *mine = false;
    if (same_word(s, n, "xdef") || same_word(s, n, ".xdef")
        || same_word(s, n, ".global") || same_word(s, n, ".globl")) {
        *mine = true;

        return link_names(p, e, stop, SYM_XDEF);
    }
    if (same_word(s, n, "xref") || same_word(s, n, ".xref")
        || same_word(s, n, ".extern")) {
        *mine = true;

        return link_names(p, e, stop, SYM_XREF);
    }
    uint8_t k;
    if (same_word(s, n, "segment") || same_word(s, n, ".segment")
        || same_word(s, n, ".section")) {
        *mine = true;
        const char* end;
        const char* name = seg_token(p, e, &end);
        k = seg_of(name, (int) (end - name));
        if (k == 0) {
            err_tok(name, (int) (end - name));
            state.err = ZAP_E_OBJ_UNKNOWN_SEGMENT;

            return false;
        }
        p = end;
    } else if (n > 1 && *s == '.' && (k = seg_of(s, n)) != 0) {
        /* `.text`, `.data`, `.bss` and `.rodata` on their own. The dot is
         * required: a bare `data` is too likely to be something else. */
        *mine = true;
    } else {
        return true;
    }
    *stop = p;

    return seg_switch(k);
}

/* ----------------------------------------------------------------------
 * ELF
 *
 * An ELF32 relocatable file, as agondev's `ld` reads it: the four segments as
 * `.text`, `.data`, `.bss` and `.rodata`, a `.rela` section beside each one
 * that has relocations, a symbol table, and the two string tables.
 *
 * The symbol table is the null symbol and a section symbol for each segment,
 * which are local, then the exports and the imports a relocation uses, sorted
 * by name. A relocation against a label defined here is against its section
 * symbol, as GNU `as` writes one; only an import is named.
 *
 * Every number in the file fits in 24 bits except an addend, so the 32-bit
 * fields are written as three bytes and a zero, which on the eZ80 keeps this
 * off the 32-bit helper calls.
 * ---------------------------------------------------------------------- */

#define ELF_EHSIZE    52

#define ELF_SHENTSIZE 40

#define ELF_SYMSIZE   16

#define ELF_RELASIZE  12

#define EM_Z80        220

/* EF_Z80_EZ80 | EF_Z80_ADL, as agondev's compiler marks its own objects. */
#define ELF_FLAGS     0x84

#define SHN_ABS       0xFFF1

/* A symbol index shares a 24-bit field with the relocation type's byte. */
#define ELF_SYM_MAX   0x7FFF

static const char shstrtab[] =
    "\0.text\0.data\0.bss\0.rodata\0.symtab\0.strtab\0.shstrtab"
    "\0.rela.text\0.rela.data\0.rela.rodata";

/* Where each name starts in shstrtab. */
#define SHN_TEXT      1

#define SHN_SYMTAB    26

#define SHN_STRTAB    34

#define SHN_SHSTRTAB  42

#define SHN_RELA      52    /* ".rela.text"; the other two follow */

static const uint8_t seg_shname[SEG_COUNT] = {0, 1, 7, 13, 18};

static const uint8_t rela_shname[SEG_COUNT] = {0, 52, 63, 0, 74};

static void le16(uint8_t* p, int v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void le32(uint8_t* p, int v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = 0;
}

/* All four bytes, for the two numbers that can be negative or wide: an
 * addend, and the value of an exported EQU. */
static void le32v(uint8_t* p, evalue v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static bool put(const void* p, int n, int* pos) {
    if (n == 0) {
        return true;
    }
    if ((int) mos_fwrite(state.out_fh, (char*) p, (uint24_t) n) != n) {
        state.err = ZAP_E_CANNOT_WRITE_OUTPUT;

        return false;
    }
    *pos += n;

    return true;
}

static bool pad4(int* pos) {
    static const uint8_t zero[4] = {0, 0, 0, 0};

    return put(zero, (-*pos) & 3, pos);
}

static void shdr(uint8_t* h, int name, int type, int flags, int off, int size,
                 int link, int info, int align, int entsize) {
    memset(h, 0, ELF_SHENTSIZE);
    le32(h + 0, name);
    le32(h + 4, type);
    le32(h + 8, flags);
    le32(h + 16, off);
    le32(h + 20, size);
    le32(h + 24, link);
    le32(h + 28, info);
    le32(h + 32, align);
    le32(h + 36, entsize);
}

/* The blocks whose fill named a label further down. A flat assembly writes
 * these in its sweep at the end; an object has all of its bytes in memory. */
static void obj_fills(void) {
    for (int i = 0; i < state.fillp_used; i++) {
        const fillpatch* fp = &state.fillp[i];
        uint8_t* at = obj_ptr(fp->off);
        const evalue v = fp->sp->addr;
        const int n = fp->count * fp->width;
        for (int k = 0; k < n; k++) {
            at[k] = (uint8_t) (v >> (8 * (k % fp->width)));
        }
    }
}

/* By name, byte for byte, the shorter first where one is the start of the
 * other. */
static int name_cmp(const void* a, const void* b) {
    const sym* x = *(const sym* const*) a;
    const sym* y = *(const sym* const*) b;
    const int n = x->len < y->len ? x->len : y->len;
    for (int i = 0; i < n; i++) {
        if (x->name[i] != y->name[i]) {
            return (uint8_t) x->name[i] - (uint8_t) y->name[i];
        }
    }

    return x->len - y->len;
}

static int reloc_cmp(const void* a, const void* b) {
    const int x = ((const objreloc*) a)->off;
    const int y = ((const objreloc*) b)->off;

    return x < y ? -1 : x > y;
}

/* The symbols past the section symbols, in the order they are written. */
static const sym** globals;

static bool elf_write(int* written) {
    static const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 1, 1, 1};
    static const uint8_t sflags[SEG_COUNT] = {0, 6, 3, 3, 2};
    /* Static: at this size it would take the frame far past what an `ix`
     * displacement reaches. */
    static uint8_t buf[ELF_SHENTSIZE];
    int pos = 0;

    /* The globals: every export, and every import a relocation names. */
    int nglob = 0;
    Z_SITE("object symbols");
    globals = (const sym**) malloc((size_t) (link_used + 1) * sizeof(sym*));
    if (globals == NULL) {
        state.err = ZAP_E_OUT_MEMORY_LABELS;

        return false;
    }
    int strsize = 1;
    for (int i = 0; i < link_used; i++) {
        const sym* sp = links[i].sp;
        if ((sp->reloc & SYM_XDEF) != 0 || (sp->reloc & SYM_USED) != 0) {
            globals[nglob++] = sp;
            strsize += sp->len + 1;
        }
    }
    if (nglob + SEG_COUNT > ELF_SYM_MAX) {
        state.err = ZAP_E_OBJ_TOO_MANY_SYMBOLS;

        return false;
    }
    qsort(globals, (size_t) nglob, sizeof(sym*), name_cmp);
    /* An import's address is 0, and nothing reads it now that every fixup
     * is settled, so it holds the symbol's index for its relocations. */
    for (int i = 0; i < nglob; i++) {
        if ((globals[i]->reloc & SYM_XREF) != 0) {
            ((sym*) globals[i])->addr = SEG_COUNT + i;
        }
    }
    qsort(relocs, (size_t) reloc_used, sizeof(objreloc), reloc_cmp);

    /* Where everything goes, worked out before a byte is written: the
     * header, the segments' bytes, the symbol table and the strings, then
     * the relocations, then the section headers. */
    int off[SEG_COUNT];
    int rfirst[SEG_COUNT];
    int rcount[SEG_COUNT];
    int at = ELF_EHSIZE;
    for (int k = 1; k < SEG_COUNT; k++) {
        off[k] = at;
        if (k != SEG_BSS) {
            at += segs[k].len;
        }
        rfirst[k] = 0;
        rcount[k] = 0;
    }
    for (int i = reloc_used - 1; i >= 0; i--) {
        const int k = relocs[i].off >> SEG_SHIFT;
        rfirst[k] = i;
        rcount[k]++;
    }
    const int symoff = (at + 3) & ~3;
    const int symsize = (SEG_COUNT + nglob) * ELF_SYMSIZE;
    const int stroff = symoff + symsize;
    const int shstroff = stroff + strsize;
    int relaoff = (shstroff + (int) sizeof(shstrtab) + 3) & ~3;
    int nrela = 0;
    for (int k = 1; k < SEG_COUNT; k++) {
        if (rcount[k] != 0) {
            nrela++;
        }
    }
    const int shoff = relaoff + reloc_used * ELF_RELASIZE;
    const int shnum = SEG_COUNT + nrela + 3;
    const int symtab = SEG_COUNT + nrela;

    memset(buf, 0, ELF_SHENTSIZE);
    uint8_t* h = buf;
    memset(h, 0, 16);
    memcpy(h, ident, sizeof(ident));
    if (!put(h, 16, &pos)) {
        return false;
    }
    memset(h, 0, ELF_EHSIZE - 16);
    le16(h + 0, 1);             /* ET_REL */
    le16(h + 2, EM_Z80);
    le32(h + 4, 1);             /* EV_CURRENT */
    le32(h + 16, shoff);
    le32(h + 20, ELF_FLAGS);
    le16(h + 24, ELF_EHSIZE);
    le16(h + 30, ELF_SHENTSIZE);
    le16(h + 32, shnum);
    le16(h + 34, symtab + 2);
    if (!put(h, ELF_EHSIZE - 16, &pos)) {
        return false;
    }
    for (int k = 1; k < SEG_COUNT; k++) {
        if (k != SEG_BSS && !put(segs[k].buf, segs[k].len, &pos)) {
            return false;
        }
    }

    /* The symbol table. */
    if (!pad4(&pos)) {
        return false;
    }
    for (int k = 0; k < SEG_COUNT; k++) {
        memset(h, 0, ELF_SYMSIZE);
        if (k != 0) {
            h[12] = 3;          /* STB_LOCAL, STT_SECTION */
            le16(h + 14, k);
        }
        if (!put(h, ELF_SYMSIZE, &pos)) {
            return false;
        }
    }
    int name = 1;
    for (int i = 0; i < nglob; i++) {
        const sym* sp = globals[i];
        memset(h, 0, ELF_SYMSIZE);
        le32(h + 0, name);
        name += sp->len + 1;
        h[12] = 0x10;           /* STB_GLOBAL, STT_NOTYPE */
        if ((sp->reloc & SYM_PLACED) != 0) {
            le32(h + 4, (int) sp->addr & SEG_MAX);
            le16(h + 14, (int) (sp->addr >> SEG_SHIFT));
        } else if (sp->defined) {
            le32v(h + 4, sp->addr);
            le16(h + 14, SHN_ABS);
        }
        if (!put(h, ELF_SYMSIZE, &pos)) {
            return false;
        }
    }

    /* The names, then the section names. */
    h[0] = 0;
    if (!put(h, 1, &pos)) {
        return false;
    }
    for (int i = 0; i < nglob; i++) {
        if (!put(globals[i]->name, globals[i]->len, &pos) || !put(h, 1, &pos)) {
            return false;
        }
    }
    if (!put(shstrtab, (int) sizeof(shstrtab), &pos) || !pad4(&pos)) {
        return false;
    }

    /* The relocations, grouped by segment because they were sorted by
     * position and a position starts with its segment. */
    for (int i = 0; i < reloc_used; i++) {
        const objreloc* r = &relocs[i];
        const int sidx = (r->base->reloc & SYM_XREF) != 0
                             ? (int) r->base->addr
                             : (int) (r->base->addr >> SEG_SHIFT);
        memset(h, 0, ELF_RELASIZE);
        le32(h + 0, r->off & SEG_MAX);
        le32(h + 4, (sidx << 8) | r->type);
        le32v(h + 8, r->addend);
        if (!put(h, ELF_RELASIZE, &pos)) {
            return false;
        }
    }

    /* The section headers: the null one, the segments, their relocations,
     * and the three tables. */
    shdr(h, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!put(h, ELF_SHENTSIZE, &pos)) {
        return false;
    }
    for (int k = 1; k < SEG_COUNT; k++) {
        const int size = k == SEG_BSS ? segs[k].pend : segs[k].len;
        shdr(h, seg_shname[k], k == SEG_BSS ? 8 : 1, sflags[k], off[k], size,
             0, 0, segs[k].align, 0);
        if (!put(h, ELF_SHENTSIZE, &pos)) {
            return false;
        }
    }
    for (int k = 1; k < SEG_COUNT; k++) {
        if (rcount[k] == 0) {
            continue;
        }
        /* SHT_RELA, and SHF_INFO_LINK: sh_info names the section it
         * applies to. */
        shdr(h, rela_shname[k], 4, 0x40, relaoff + rfirst[k] * ELF_RELASIZE,
             rcount[k] * ELF_RELASIZE, symtab, k, 4, ELF_RELASIZE);
        if (!put(h, ELF_SHENTSIZE, &pos)) {
            return false;
        }
    }
    shdr(h, SHN_SYMTAB, 2, 0, symoff, symsize, symtab + 1, SEG_COUNT, 4, ELF_SYMSIZE);
    if (!put(h, ELF_SHENTSIZE, &pos)) {
        return false;
    }
    shdr(h, SHN_STRTAB, 3, 0, stroff, strsize, 0, 0, 1, 0);
    if (!put(h, ELF_SHENTSIZE, &pos)) {
        return false;
    }
    shdr(h, SHN_SHSTRTAB, 3, 0, shstroff, (int) sizeof(shstrtab), 0, 0, 1, 0);
    if (!put(h, ELF_SHENTSIZE, &pos)) {
        return false;
    }
    *written = pos;

    return true;
}

/* The object, once the assembly has succeeded. */
bool obj_write(int* written) {
    obj_fills();
    if (!out_create()) {
        return false;
    }
    const bool ok = elf_write(written);
    free(globals);
    globals = NULL;

    return ok;
}
