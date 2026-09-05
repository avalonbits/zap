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
    FIX_ABS8 = 0,   /* one byte, an 8-bit immediate */
    FIX_ABS16,      /* two-byte address, Z80 mode or a short suffix */
    FIX_ABS24,      /* three-byte address, ADL mode */
    FIX_ABS32,      /* four bytes, for dw32 */
    FIX_REL8,       /* one signed byte, relative to the next instruction */

    /* A value that folds into the opcode byte itself -- rst, bit, im, set,
     * res. There is no hole to leave, but there does not need to be one: the
     * byte is written with the operand contributing nothing, which is exactly
     * the base the transform ORs into, so settling it later ORs in the same
     * bits the immediate path would have. next_ carries the transform and the
     * range check to re-run. */
    FIX_FOLD,

    /* A run of identical elements whose value is not known yet: blkb and its
     * wider forms, where the count is known now but the fill is not. One kind
     * per element width, so the width needs no field of its own; the element
     * count rides in next_, which only FIX_REL8 otherwise uses. */
    FIX_FILL1,
    FIX_FILL2,
    FIX_FILL3,
    FIX_FILL4
} fixup_kind;

typedef struct _label_node {
    /* Where the deferred expression's text lives in the stack's arena. The
     * whole expression is kept, not just a name: "ld bc, end - start" with
     * both labels defined further down is how a length is written, and it is
     * in nearly every real program. Storing it as text and re-evaluating it
     * once the symbols are known keeps that working in one pass. */
    int text_off_;

    /* An operand's expression is captured into a 128-byte buffer, so its
     * length is a byte. A whole int here cost two bytes a node on the eZ80,
     * and a large program holds thousands of these at once: BBC BASIC has
     * 2,149 live. */
    uint8_t text_len_;

    int bpos_;   /* offset in the output buffer to patch */
    /* Address of the instruction after this one, for FIX_REL8. For the
     * FIX_FILL kinds it is instead how many elements to write. */
    int next_;
    int here_;   /* the statement's address, so '$' still means something */

    /* The source line, for the diagnostic. 24 bits of it is more line numbers
     * than a file the reader will accept can have. */
    uint16_t line_;

    uint8_t kind_;
    uint16_t scope_;  /* which local scope the names were written in */

    /* For a forward reference to an anonymous label (@f / @n), which one in
     * source order it means. -1 for an ordinary expression. */
    int anon_;

    /* Which symbol this is waiting on, as the hash of its scoped key, and the
     * next fixup waiting on the same one.
     *
     * A fixup used to sit here until the end of the file even when the label
     * it wanted appeared on the next line, so a program held every forward
     * reference it had ever made: BBC BASIC peaked at 2,149 of them. Indexing
     * them by what they are waiting for means a fixup is retired when its
     * symbol is defined, and the live count follows how far forward references
     * reach rather than how long the file is.
     *
     * The hash rather than the name because a name is up to 64 bytes and this
     * is two. A collision costs one wasted attempt, which is harmless: the
     * expression is re-evaluated and simply stays pending. */
    uint16_t wait_;

    /* Doubly linked, so retiring one is O(1). Singly linked, ls_retire had to
     * walk the bucket from its head to find the node before it -- and that ran
     * on every retirement, 2,140 of them for BBC BASIC, each a scattered read
     * into an array of 26-byte nodes on a machine with no cache. prev_ is also
     * the free-list link once a slot is retired, since nothing else needs it
     * then. */
    int link_;
    int prev_;
} label_node;

/* Buckets for that index. Small on purpose: it is walked on every label
 * definition, and a collision only costs a re-evaluation. */
#define LS_WAIT_BUCKETS 128

typedef struct _label_stack {
    label_node* nodes_;
    int sz_;

    /* High-water mark of slots ever used, and the head of the list of slots
     * freed by retirement. Retired slots are reused before the array grows,
     * which is what keeps it small. A slot with text_len_ == 0 is free; a real
     * fixup always has at least one character of expression. */
    int pos_;
    /* The size to allocate on the first push, kept until then. */
    int want_;

    int free_;
    int live_;

    int heads_[LS_WAIT_BUCKETS];

    /* Expression texts, packed end to end. Most are a few characters, so an
     * arena costs far less than a fixed field on every node. */
    char* text_;
    int text_len_;
    int text_cap_;
} label_stack;

label_stack* ls_init(label_stack* ls, int sz);
void ls_destroy(label_stack* ls);

bool ls_push(label_stack* ls, const char* text, int sz, int bpos,
             int next, int here, int line, fixup_kind kind, uint16_t scope,
             int anon, uint16_t wait);

/* The first fixup waiting on this hash, and the next after it. Walk with
 * ls_at to read one. Retiring during the walk is safe: take the next index
 * before retiring the current. */
int ls_waiting_on(const label_stack* ls, uint16_t wait);
int ls_next_waiting(const label_stack* ls, int idx);
const label_node* ls_at(const label_stack* ls, int idx);

/* Frees a slot and unlinks it from its bucket. */
void ls_retire(label_stack* ls, int idx);

/* Iterates whatever is still pending, for the end of the assembly. */
int ls_first_live(const label_stack* ls);
int ls_next_live(const label_stack* ls, int idx);
int ls_live_count(const label_stack* ls);

/* The stored expression text for a node. */
const char* ls_text(const label_stack* ls, const label_node* n);
const label_node* ls_pop(label_stack* ls);

#endif  // _LABEL_STACK_H_
