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
        sp->reloc = k != 0;
        sp->addr = k << SEG_SHIFT;
    }
    cur = 0;
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

/* A fixup's value in an object, and whether it can be written as a number.
 *
 * A label placed in a segment counts once, and one subtracted counts minus
 * once. Where the count comes to nothing the value is a plain number -- a
 * constant, or the distance between two labels in the same segment, where
 * the segments cancel. A relative jump to a label in its own segment is a
 * distance too. Anything else needs a relocation, which zap does not write
 * yet, or is not something an object can express at all. */
bool obj_value(const fixup* f, evalue* val) {
    const sym* t = f->target;
    const sym* s = f->sub;
    if (!t->defined && !t->reloc) {
        state.line = f->line;
        err_tok(t->name, t->len);
        state.err = ZAP_E_UNKNOWN_LABEL;

        return false;
    }
    if (s != NULL && !s->defined && !s->reloc) {
        state.line = f->line;
        err_tok(s->name, s->len);
        state.err = ZAP_E_UNKNOWN_LABEL;

        return false;
    }

    evalue v = t->addr + f->addend;
    int net = 0;
    int seg = 0;
    if (t->reloc) {
        net = 1;
        seg = (int) (t->addr >> SEG_SHIFT);
    }
    if (s != NULL) {
        const bool minus = (f->width & FIX_SUB2) != 0;
        v += minus ? -s->addr : s->addr;
        if (s->reloc) {
            const int ss = (int) (s->addr >> SEG_SHIFT);
            if (minus && net == 1 && ss == seg) {
                net = 0;
            } else if (!minus && net == 0) {
                net = 1;
                seg = ss;
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
        if (net == 1 && seg == (f->off >> SEG_SHIFT)) {
            return true;
        }
        state.line = f->line;
        state.err = net == 2 ? ZAP_E_OBJ_NOT_RELOCATABLE : ZAP_E_OBJ_NO_RELOCATIONS_YET;

        return false;
    }
    if (net == 0) {
        return true;
    }
    state.line = f->line;
    state.err = (net == 1 && w <= 4) ? ZAP_E_OBJ_NO_RELOCATIONS_YET
                                     : ZAP_E_OBJ_NOT_RELOCATABLE;

    return false;
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
 * `.text`, `.data`, `.bss` and `.rodata`, a symbol table holding one section
 * symbol for each, and the two string tables. Every number in it fits in 24
 * bits, so the 32-bit fields are written as three bytes and a zero, which on
 * the eZ80 keeps this off the 32-bit helper calls.
 * ---------------------------------------------------------------------- */

#define ELF_EHSIZE    52

#define ELF_SHENTSIZE 40

#define ELF_SYMSIZE   16

#define ELF_SHNUM     8

#define ELF_SYMTAB    5

#define ELF_STRTAB    6

#define ELF_SHSTRTAB  7

#define EM_Z80        220

/* EF_Z80_EZ80 | EF_Z80_ADL, as agondev's compiler marks its own objects. */
#define ELF_FLAGS     0x84

static const char shstrtab[] =
    "\0.text\0.data\0.bss\0.rodata\0.symtab\0.strtab\0.shstrtab";

/* Where each section's name starts in shstrtab, by section index. */
static const uint8_t shname[ELF_SHNUM] = {0, 1, 7, 13, 18, 26, 34, 42};

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

static void shdr(uint8_t* h, int idx, int type, int flags, int off, int size,
                 int link, int info, int align, int entsize) {
    memset(h, 0, ELF_SHENTSIZE);
    le32(h + 0, shname[idx]);
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

static bool elf_write(int* written) {
    static const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 1, 1, 1};
    static const uint8_t sflags[SEG_COUNT] = {0, 6, 3, 3, 2};
    /* Static: at 320 bytes it would take the frame far past what an `ix`
     * displacement reaches. */
    static uint8_t buf[ELF_SHNUM * ELF_SHENTSIZE];
    int pos = 0;

    /* Where everything goes, worked out before a byte is written. */
    int off[SEG_COUNT];
    int at = ELF_EHSIZE;
    for (int k = 1; k < SEG_COUNT; k++) {
        off[k] = at;
        if (k != SEG_BSS) {
            at += segs[k].len;
        }
    }
    const int symoff = (at + 3) & ~3;
    const int symsize = SEG_COUNT * ELF_SYMSIZE;
    const int stroff = symoff + symsize;
    const int shstroff = stroff + 1;
    const int shoff = (shstroff + (int) sizeof(shstrtab) + 3) & ~3;

    memset(buf, 0, ELF_EHSIZE);
    memcpy(buf, ident, sizeof(ident));
    le16(buf + 16, 1);          /* ET_REL */
    le16(buf + 18, EM_Z80);
    le32(buf + 20, 1);          /* EV_CURRENT */
    le32(buf + 32, shoff);
    le32(buf + 36, ELF_FLAGS);
    le16(buf + 40, ELF_EHSIZE);
    le16(buf + 46, ELF_SHENTSIZE);
    le16(buf + 48, ELF_SHNUM);
    le16(buf + 50, ELF_SHSTRTAB);
    if (!put(buf, ELF_EHSIZE, &pos)) {
        return false;
    }
    for (int k = 1; k < SEG_COUNT; k++) {
        if (k != SEG_BSS && !put(segs[k].buf, segs[k].len, &pos)) {
            return false;
        }
    }

    /* The symbol table: the null symbol, and a section symbol for each
     * segment -- all local, so the first global is one past them. */
    if (!pad4(&pos)) {
        return false;
    }
    memset(buf, 0, (size_t) symsize);
    for (int k = 1; k < SEG_COUNT; k++) {
        uint8_t* e = buf + k * ELF_SYMSIZE;
        e[12] = 3;              /* STB_LOCAL, STT_SECTION */
        le16(e + 14, k);
    }
    buf[symsize] = 0;           /* .strtab: the empty name, and nothing else */
    if (!put(buf, symsize + 1, &pos) || !put(shstrtab, (int) sizeof(shstrtab), &pos)
        || !pad4(&pos)) {
        return false;
    }

    shdr(buf, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    for (int k = 1; k < SEG_COUNT; k++) {
        const int size = k == SEG_BSS ? segs[k].pend : segs[k].len;
        shdr(buf + k * ELF_SHENTSIZE, k, k == SEG_BSS ? 8 : 1, sflags[k], off[k],
             size, 0, 0, segs[k].align, 0);
    }
    shdr(buf + ELF_SYMTAB * ELF_SHENTSIZE, ELF_SYMTAB, 2, 0, symoff, symsize,
         ELF_STRTAB, SEG_COUNT, 4, ELF_SYMSIZE);
    shdr(buf + ELF_STRTAB * ELF_SHENTSIZE, ELF_STRTAB, 3, 0, stroff, 1, 0, 0, 1, 0);
    shdr(buf + ELF_SHSTRTAB * ELF_SHENTSIZE, ELF_SHSTRTAB, 3, 0, shstroff,
         (int) sizeof(shstrtab), 0, 0, 1, 0);
    if (!put(buf, ELF_SHNUM * ELF_SHENTSIZE, &pos)) {
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

    return elf_write(written);
}
