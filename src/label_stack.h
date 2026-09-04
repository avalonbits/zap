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

#ifndef _LABEL_STACK_H_
#define _LABEL_STACK_H_

#include <stdbool.h>
#include <stdint.h>

#include "value.h"

/* What to write once the label's address is known.
 *
 * An instruction's *size* never depends on a label's value on the eZ80 -- it
 * follows from the operand shapes plus the ADL mode and any suffix, all known
 * where the instruction is read. That is what lets a single pass leave a hole
 * and fill it at the end instead of needing a second pass to lay out
 * addresses. */
typedef enum _fixup_kind {
    FIX_ABS16 = 0,  /* two-byte address, Z80 mode or a short suffix */
    FIX_ABS24,      /* three-byte address, ADL mode */
    FIX_REL8        /* one signed byte, relative to the next instruction */
} fixup_kind;

typedef struct _label_node {
    char label_[MAX_NAME + 1];

    int bpos_;   /* offset in the output buffer to patch */
    int next_;   /* address of the instruction after this one, for FIX_REL8 */
    int line_;   /* source line, so the error points at the reference */

    uint8_t kind_;
    uint8_t scope_;  /* which local scope the name was written in */

    /* For a forward reference to an anonymous label (@f / @n), which one in
     * source order it means. -1 for an ordinary name. */
    int anon_;
} label_node;

typedef struct _label_stack {
    label_node* nodes_;
    int sz_;
    int pos_;
} label_stack;

label_stack* ls_init(label_stack* ls, int sz);
void ls_destroy(label_stack* ls);

bool ls_push(label_stack* ls, const char* label, int sz, int bpos,
             int next, int line, fixup_kind kind, uint8_t scope, int anon);
const label_node* ls_pop(label_stack* ls);

#endif  // _LABEL_STACK_H_
