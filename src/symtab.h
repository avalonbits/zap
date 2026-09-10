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

#ifndef SYMTAB_H
#define SYMTAB_H

#include "zap.h"

/* Inlined into callers in other files, so the bodies live here. */

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

#endif /* SYMTAB_H */
