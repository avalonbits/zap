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
#include "directive.h"
#include "expr.h"
#include "insn.h"
#include "scan.h"
#include "symtab.h"

/* ======================================================================
 * OUTPUT
 *
 * The buffer the assembled bytes are built in, and the reserved runs that
 * `DS` and `ALIGN` leave in it.
 * ====================================================================== */

static bool out_settle(void);

/* Writes the window out and starts it again at the beginning.
 *
 * The whole window, never part of it. Nothing reads backwards in it -- a
 * reservation is a count now, and a patch that falls behind the window is
 * recorded instead -- so there is nothing to keep, and a partial flush would
 * leave every later write straddling a sector. 64 KB is 128 whole ones.
 *
 * The file is opened here rather than at startup, so that an assembly which
 * fails before it fills a window leaves no output file at all. FA_READ as well
 * as FA_WRITE: the sweep at the end reads chunks back to patch them, and
 * FatFS refuses a read on a handle that was not opened for it. */
bool out_create(void) {
    if (state.out_fh != 0) {
        return true;
    }
    state.out_fh = mos_fopen(state.out_path,
                             FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
    if (state.out_fh == 0) {
        state.err = ZAP_E_CANNOT_WRITE_OUTPUT;

        return false;
    }

    return true;
}

bool out_flush(void) {
    const int n = (int) (state.o - state.win);
    if (n == 0) {
        return true;
    }
    if (!out_create()) {
        return false;
    }
    /* Positioned rather than appended. The listing reads bytes back out of
     * this file while the assembly is still running -- a line whose bytes were
     * flushed before it was listed -- and a read leaves the file wherever it
     * finished. Without this the next window lands on top of one already
     * written. */
    if (mos_flseek(state.out_fh, (uint32_t) state.wbase) != 0) {
        state.err = ZAP_E_CANNOT_WRITE_OUTPUT;

        return false;
    }
    if ((int) mos_fwrite(state.out_fh, (char*) state.win, (uint24_t) n) != n) {
        state.err = ZAP_E_CANNOT_WRITE_OUTPUT;

        return false;
    }
    state.wbase += n;
    state.o = state.win;

    return true;
}

/* Closes the output and takes it away again, for an assembly that failed after
 * it had already written part of itself out. */
void out_discard(void) {
    if (state.out_fh == 0) {
        return;
    }
    mos_fclose(state.out_fh);
    state.out_fh = 0;
    mos_del(state.out_path);
}

/* Reads `n` bytes of the output at `off`, from wherever they are.
 *
 * The range may straddle the window: a listed line whose first bytes were
 * flushed while the rest of it was still being assembled wants both halves,
 * and asking the file for the half it does not have is a short read. */
bool out_peek(int off, uint8_t* dst, int n) {
    if (off < state.wbase) {
        int from_file = state.wbase - off;
        if (from_file > n) {
            from_file = n;
        }
        if (state.out_fh == 0 || mos_flseek(state.out_fh, (uint32_t) off) != 0
            || (int) mos_fread(state.out_fh, (char*) dst, (uint24_t) from_file)
                   != from_file) {
            state.err = ZAP_E_CANNOT_READ_OUTPUT;

            return false;
        }
        dst += from_file;
        off += from_file;
        n -= from_file;
    }
    if (n > 0) {
        memcpy(dst, state.win + (off - state.wbase), (size_t) n);
    }

    return true;
}

/* Remembers a patch to output the window has passed.
 *
 * Ascending while one settling pass is running -- fixups are settled in the
 * order they were created, which is the order they were emitted in -- and a
 * step backwards where a new pass begins: a scope ending, a sweep, the
 * globals at the end of the source. The step is where a run starts, and
 * noticing it here is the whole of what keeps resolve_late's cursors honest.
 * A record appended below the one before it with no run opened for it would
 * be walked past in silence and its bytes never written. */
static bool late_run_open(int off) {
    /* The first record opens the first run, so that every run is in the array
     * and resolve_late has one place to look rather than a special case for
     * the one that starts at zero. */
    if (state.late_used != 0 && off >= state.late[state.late_used - 1].off) {
        return true;
    }
    if (state.late_runs == state.late_runcap) {
        Z_SITE("late patches");
        const int want = state.late_runcap == 0 ? 8 : state.late_runcap + state.late_runcap;
        laterun* grown =
            (laterun*) realloc(state.late_run, (size_t) want * sizeof(laterun));
        if (grown == NULL) {
            state.err = ZAP_E_OUT_MEMORY_LABELS;

            return false;
        }
        state.late_run = grown;
        state.late_runcap = want;
    }
    state.late_run[state.late_runs].start = state.late_used;
    state.late_run[state.late_runs].cur = state.late_used;
    state.late_runs++;

    return true;
}

bool out_late(int off, uint8_t kind, const uint8_t* b) {
    if (!late_run_open(off)) {
        return false;
    }
    if (state.late_used == state.late_cap) {
        Z_SITE("late patches");
        const int want = state.late_cap == 0 ? 64 : state.late_cap + state.late_cap;
        latepatch* grown =
            (latepatch*) realloc(state.late, (size_t) want * sizeof(latepatch));
        if (grown == NULL) {
            state.err = ZAP_E_OUT_MEMORY_LABELS;

            return false;
        }
        state.late = grown;
        state.late_cap = want;
    }
    latepatch* r = &state.late[state.late_used++];
    r->off = off;
    r->kind = kind;
    r->b[0] = b[0];
    r->b[1] = b[1];
    r->b[2] = b[2];
    r->b[3] = b[3];

    return true;
}

/* Room for one instruction. */
bool out_reserve(void) {
    /* Asking for room is the signal that a byte is about to be written, and
     * that is the moment the reference fills whatever was reserved. Every
     * emitter asks here first, so this is the only place that has to know. */
    if (state.pend != 0 && !out_settle()) {
        return false;
    }
    /* The output as a whole is bounded by the width of int, which on the eZ80
     * is three bytes: past OUT_TOTAL_MAX the position wraps negative and the
     * file corrupts without a word -- labels land on the wrong bytes, patches
     * reach backwards. The ceiling is on the right of the comparison, less
     * the room this call grants, rather than added to the position: position
     * plus 13 wraps negative in int when the position is within 13 of the
     * ceiling, and a negative compares less than the ceiling and slips
     * through. Subtracted from the constant there is no addition to wrap, and
     * out_here() itself never wraps -- this check is what keeps it under the
     * ceiling, so it holds at every entry here. */
    if (out_here() > (int) (OUT_TOTAL_MAX - OUT_MAX_INSN)) {
        state.err = ZAP_E_OUTPUT_PAST_24_BIT_RANGE;

        return false;
    }
    if (state.o <= state.lim) {
        return true;
    }

    return out_flush();
}

/* Room for `n` bytes, for the directives, which are the only things that write
 * more than an instruction's worth at once.
 *
 * `n` may not exceed the window. The three callers that could ask for more --
 * ORG padding, BLK and INCBIN -- write in window-sized pieces instead, because
 * there is no longer anywhere for a larger request to go. */
static bool out_reserve_n(int n) {
    if (state.pend != 0 && !out_settle()) {
        return false;
    }
    /* The whole-output bound, in the same subtract-from-the-constant form
     * out_reserve uses: `n` here is at most a window, so ceiling minus n
     * cannot wrap, and the position is already under the ceiling. The cold
     * callers below keep the evalue form, because their `n` is an expression
     * value that can be wider than int itself. */
    if (out_here() > (int) OUT_TOTAL_MAX - n) {
        state.err = ZAP_E_OUTPUT_PAST_24_BIT_RANGE;

        return false;
    }
    if (state.o + n <= state.lim) {
        return true;
    }
    if (!out_flush()) {
        return false;
    }
    if (state.o + n > state.lim) {
        /* A caller asked for more than a window and did not chunk it. */
        state.err = ZAP_E_OUT_MEMORY_OUTPUT;

        return false;
    }

    return true;
}

/* Case-insensitive against a lower-case literal of known length. Deliberately
 * not `same_ci`: that one is inlined into mnemonic_of and stays that way only
 * while nothing cold calls it, which is a lesson this file has now learned
 * three times. */
static bool dir_is(const char* s, const char* want, int n) {
    for (int i = 0; i < n; i++) {
        if ((s[i] | 0x20) != want[i]) {
            return false;
        }
    }

    return true;
}

uint8_t directive_of(const char* s, int n) {
    if (*s == '.') {
        s++;
        n--;
    }
    switch (n) {
        case 2:
            if (dir_is(s, "if", 2)) return DIR_IF;
            if (dir_is(s, "db", 2)) return DIR_DB;
            if (dir_is(s, "dw", 2)) return DIR_DW;
            if (dir_is(s, "dl", 2)) return DIR_DL;
            if (dir_is(s, "ds", 2)) return DIR_DS;
            break;
        case 4:
            if (dir_is(s, "else", 4)) return DIR_ELSE;
            if (dir_is(s, "defb", 4)) return DIR_DB;
            if (dir_is(s, "byte", 4)) return DIR_DB;
            if (dir_is(s, "defw", 4)) return DIR_DW;
            if (dir_is(s, "dw24", 4)) return DIR_DL;
            if (dir_is(s, "dw32", 4)) return DIR_DW32;
            if (dir_is(s, "defs", 4)) return DIR_DS;
            if (dir_is(s, "blkb", 4)) return DIR_BLKB;
            if (dir_is(s, "blkw", 4)) return DIR_BLKW;
            if (dir_is(s, "blkp", 4)) return DIR_BLKP;
            if (dir_is(s, "blkl", 4)) return DIR_BLKL;
            break;
        case 3:
            if (dir_is(s, "org", 3)) return DIR_ORG;
            if (dir_is(s, "cpu", 3)) return DIR_CPU;
            break;
        case 5:
            if (dir_is(s, "endif", 5)) return DIR_ENDIF;
            if (dir_is(s, "macro", 5)) return DIR_MACRO;
            if (dir_is(s, "ascii", 5)) return DIR_DB;
            if (dir_is(s, "asciz", 5)) return DIR_ASCIZ;
            if (dir_is(s, "align", 5)) return DIR_ALIGN;
            break;
        case 6:
            if (dir_is(s, "incbin", 6)) return DIR_INCBIN;
            if (dir_is(s, "assume", 6)) return DIR_ASSUME;
            break;
        case 7:
            if (dir_is(s, "include", 7)) return DIR_INCLUDE;
            break;
        case 8:
            if (dir_is(s, "endmacro", 8)) return DIR_ENDMACRO;
            if (dir_is(s, "fillbyte", 8)) return DIR_FILLBYTE;
            if (dir_is(s, "relocate", 8)) return DIR_RELOCATE;
            break;
        case 11:
            if (dir_is(s, "endrelocate", 11)) return DIR_ENDRELOCATE;
            break;
        default:
            break;
    }

    return DIR_NONE;
}

/* One escape, after the backslash. Returns -1 for anything not listed, which
 * the reference calls "Illegal escape code in string" -- including `\0` and
 * `\x41`, which C programmers expect and the reference does not take.
 *
 * The same set applies inside a character literal: `'\n'` is 0x0A and `'\?'`
 * is 0x3F, one table for both, as there. */
int str_escape(char c) {
    switch (c) {
        case 'n':  return 0x0A;
        case 't':  return 0x09;
        case 'r':  return 0x0D;
        case 'a':  return 0x07;
        case 'b':  return 0x08;
        case 'f':  return 0x0C;
        case 'v':  return 0x0B;
        case 'e':  return 0x1B;
        case '\\': return 0x5C;
        case '"':  return 0x22;
        case '\'': return 0x27;
        case '?':  return 0x3F;
        default:   return -1;
    }
}

/* A double-quoted string, written out as bytes.
 *
 * Reserved in one go before anything is written: the length is known once the
 * closing quote is found, and escapes only ever shrink it. */
/* Out of line, deliberately. Inlined into directive_line it would share a
 * frame deep enough that the scan spills its pointer and its cursor on every
 * character. One call per string item is far cheaper than that. */
__attribute__((noinline))
static bool emit_string(const char** pp, const char* e) {
    const char* p = *pp + 1;                      /* past the opening quote */
    /* No bound on the step past an escape. The buffer ends in a newline one
     * byte past the last valid character, so a backslash in the last position
     * steps over the sentinel and the `q < e` test above ends the scan -- as
     * "string not terminated", which is what it is. */
    const char* q = p;
    while (q < e && *q != '"' && *q != '\n') {
        q += (*q == '\\') ? 2 : 1;
    }
    if (q >= e || *q != '"') {
        state.err = ZAP_E_STRING_NOT_TERMINATED;

        return false;
    }
    if (!out_reserve_n((int) (q - p))) {
        return false;
    }

    uint8_t* o = state.o;
    while (p < q) {
        if (*p == '\\') {
            const int v = str_escape(p[1]);
            if (v < 0) {
                state.err = ZAP_E_BAD_ESCAPE_IN_STRING;

                return false;
            }
            *o++ = (uint8_t) v;
            p += 2;
        } else {
            *o++ = (uint8_t) *p++;
        }
    }
    state.o = o;
    *pp = q + 1;

    return true;
}

/* A bare name that ends the item, handed straight to the atom rather than
 * through the evaluator. The other half of what a data list holds, and the
 * same shape as lit_value above.
 *
 * The saving is not the symbol lookup, which is cheap, but the route: the
 * evaluator would run expr_value, expr_term with its unary operators and its
 * forward-reference bookkeeping, and then expr_climb to discover there is no
 * operator.
 *
 * Returns the end of the token, or NULL if this is not one. The first
 * character has to be one a name can start with, which is what keeps `$` and
 * `%1010` out; the rest of the token is left to expr_atom, which already tells
 * a trailing-h hex literal from a label and a local from a global. */
static const char* name_item(const char* p, const char* e) {
    if (!alpha_ch(*p) && *p != '_' && *p != '@') {
        return NULL;
    }
    const char* q = p;
    while (q < e && num_ch(*q)) {
        q++;
    }

    /* What ended the run has to end the item too, or an operator follows and
     * the evaluator is what reads it. */
    const char* r = q;
    while (r < e && is_space_ch(*r)) {
        r++;
    }
    if (*r != ',' && *r != '\n' && *r != ';' && r < e) {
        return NULL;
    }

    return q;
}

/* `DB`, `DW` and `DL`: a comma-separated list of values, and for DB of strings
 * too.
 *
 * A value that names a label still ahead becomes a fixup of the directive's
 * width, which is the same machinery an instruction's immediate uses and the
 * reason widths of one, two and three were already there. */
/* Left as an ordinary function even though ASCIZ gives it a second caller,
 * which elsewhere in this file is enough to make the compiler outline
 * something that should be inline. Here the call lands only on directive
 * lines rather than on every line of the source, so it costs nothing. */
static bool emit_data(uint8_t width, const char** pp, const char* e) {
    const char* p = *pp;
    for (;;) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p == '"') {
            if (width != 1) {
                /* The reference says "String type not allowed", and means it:
                 * a string is bytes and DW would have to invent a padding
                 * rule. */
                state.err = ZAP_E_STRING_DB;

                return false;
            }
            if (!emit_string(&p, e)) {
                return false;
            }
        } else {
            evalue value = 0;

            /* A plain literal, read here rather than through the evaluator,
             * and a list of four values would pay for the evaluator four
             * times.
             *
             * The same two forms the operand parser takes, for the same
             * reasons: `0x` and hex digits, or decimal with the first digit
             * outside the loop so that a one-digit value needs no multiply --
             * `d * 10` is a call to __imulu here.
             *
             * Only when what ends the digit run also ends the item: `DB 1+2`
             * goes through the evaluator, and deciding that costs one class
             * lookup on a character already in hand. */
            const char* const q = lit_value(p, e, &value);
            if (q != NULL) {
                p = q;
            } else {
                fwd_reset(NULL);
                const char* const nm = name_item(p, e);
                if (nm != NULL) {
                    if (!expr_atom(&value, p, (int) (nm - p))) {
                        return false;
                    }
                    p = nm;
                    /* `q` is NULL here, because lit_value declined, and that
                     * is what the fixup below tests: whatever this item was,
                     * it went through the atom and may have left a forward
                     * reference in the slots. */
                } else {
                    uint8_t fwdmask = 0;
                    if (!expr_value(&value, &p, e, &fwdmask)) {
                        return false;
                    }
                }
            }
            if (!out_reserve_n(width)) {
                return false;
            }
            if (q == NULL && expr_fwd != NULL) {
                const sym* target = NULL;
                const sym* sub = NULL;
                bool subneg = false;
                if (!fwd_result(&target, &sub, &subneg)) {
                    return false;
                }
                if (value < ADDEND_MIN || value > ADDEND_MAX) {
                    /* See fixup.addend. The alternative is three bytes of the
                     * constant and a silently wrong fourth, which is worse
                     * than saying so. This is the only place a value wider
                     * than the machine can reach a fixup: an instruction's
                     * immediate is three bytes by the time it gets here. */
                    state.err = ZAP_E_CONSTANT_TOO_LARGE_ADD_LABEL;

                    return false;
                }
                if (!fix_add(target, sub, (int) value,
                             (uint8_t) (width | (subneg ? FIX_SUB2 : 0)),
                             out_here())) {
                    return false;
                }
                value = 0;
            }
            /* Written in the machine's word unless the directive is wider
             * than the machine. `value >> 8` on the evaluator's word is a call
             * to __lshru; narrowed first, the shifts are ones the eZ80 has. */
            if (want_warn && !fits_width(value, width)) {
                warn_trunc(value, width);
            }

            uint8_t* o = state.o;
            if (width > 3) {
                *o++ = (uint8_t) value;
                *o++ = (uint8_t) (value >> 8);
                *o++ = (uint8_t) (value >> 16);
                *o++ = (uint8_t) (value >> 24);
            } else {
                const int v = (int) value;
                *o++ = (uint8_t) v;
                if (width > 1) {
                    *o++ = (uint8_t) (v >> 8);
                }
                if (width > 2) {
                    *o++ = (uint8_t) (v >> 16);
                }
            }
            state.o = o;
        }

        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p != ',') {
            break;
        }
        p++;
    }
    *pp = p;

    return true;
}


/* Writes `n` bytes of the FILLBYTE here and now, which is what `ORG` padding
 * is: the reference puts it down where it stands, so the value in force at
 * this moment is the value it keeps.
 *
 * Not what `DS` and `ALIGN` do -- those reserve, and reserving is `fill_take`.
 */
static bool fill_put(evalue n) {
    if (n == 0) {
        return true;
    }
    /* The bound before the first byte, in evalue for the reason out_reserve
     * gives: an ORG can ask for millions at once, which is the largest single
     * jump the position ever takes. What passes is small enough for int, and
     * the loop below counts in int. */
    if ((evalue) out_here() + n > OUT_TOTAL_MAX) {
        state.err = ZAP_E_OUTPUT_PAST_24_BIT_RANGE;

        return false;
    }
    /* In pieces, because an ORG can skip further than the window is wide and
     * the window is the only memory there is. Whole windows at a time, so each
     * flush stays a whole number of sectors.
     *
     * Not a seek past the gap: seeking beyond the end of a FatFS file and
     * writing there leaves whatever the clusters held, not the fill byte. The
     * bytes have to be written. */
    while (n > 0) {
        /* Asking for one byte is asking for room: it flushes if there is none,
         * and after a flush the room is a whole window. So the loop runs at
         * most once with less than that. */
        if (!out_reserve_n(1)) {
            return false;
        }
        const int room = (int) (state.lim - state.o);
        const int take = n < (evalue) room ? (int) n : room;
        /* memset rather than a loop: on the eZ80 it is `lddr`, and the runs
         * here are not small. An ORG that skips 96 KB spends all of its time
         * in this one line, and a character loop makes that a second and a
         * half. */
        memset(state.o, state.fill, (size_t) take);
        state.o += take;
        n -= (evalue) take;
    }

    return true;
}

/* Reserves `n` bytes without writing them, which is what `DS` and `ALIGN` do.
 * Nothing decides what they hold until something is written after them. */
static bool fill_take(evalue n) {
    if (n > 0) {
        /* The position the reservation would end at. This is the check
         * PEND_MAX used to make -- a reservation is the largest single jump
         * the position can take -- widened to the position as a whole, which
         * is what the ceiling bounds. In evalue for the reason out_reserve
         * gives. */
        if ((evalue) out_here() + n > OUT_TOTAL_MAX) {
            state.err = ZAP_E_OUTPUT_PAST_24_BIT_RANGE;

            return false;
        }
        state.pend += (int) n;
    }

    return true;
}

/* Writes out what was reserved, at the value in force now.
 *
 * `pend` is cleared before the write rather than after, so that the
 * out_reserve_n inside fill_put comes back here, finds nothing pending and
 * stops -- which is what keeps this from calling itself.
 */
static bool out_settle(void) {
    const int n = state.pend;
    if (n == 0) {
        return true;
    }
    state.pend = 0;

    return fill_put((evalue) n);
}

/* `BLKB n, fill` and its wider relatives: n units of `fill`, written out.
 *
 * Not fill_take, which reserves space that is dropped if it reaches the end of
 * the file with nothing after it. A block writes bytes and is kept wherever it
 * lands, and asking for room is what settles the reservation above it: `ds 3 /
 * blkb 3` at the end of a file is six bytes and `blkb 3 / ds 3` is three.
 *
 * Little-endian at the unit width, and the default fill is the value 0xFF
 * written at that width rather than all ones: `blkw 1` is FF 00. */
/* n and width are both positive here; the caller has checked the count. The
 * count arrives in evalue because it is an expression value: into an int it
 * would wrap on the eZ80, and a `blkb 0x800000` past eight million would
 * arrive negative and write nothing at all. */
static bool emit_block(evalue n, int width, evalue fill) {
    if (n == 0) {
        return true;
    }
    /* n times width is what the block weighs, and either is large enough to
     * take the position past the ceiling on its own. In evalue for the reason
     * out_reserve gives; what passes is small enough for int, and the loop
     * counts in int. */
    if ((evalue) out_here() + n * (evalue) width > OUT_TOTAL_MAX) {
        state.err = ZAP_E_OUTPUT_PAST_24_BIT_RANGE;

        return false;
    }
    if (want_warn && !fits_width(fill, width)) {
        warn_trunc(fill, width);
    }

    /* A unit at a time would be a reserve per unit; a whole block at once can
     * be wider than the window. So: as many whole units as the window has room
     * for, then flush and carry on. `width` is at most four, so the room is
     * never short of a single unit after a flush. */
    const int f = (int) fill;
    int left = (int) n;
    while (left > 0) {
        if (!out_reserve_n(width)) {
            return false;
        }
        int fit = (int) (state.lim - state.o) / width;
        if (fit > left) {
            fit = left;
        }
        left -= fit;

        /* Narrowed once, outside the loop, for the reason emit_data splits its
         * write: three of the four widths fit the machine and only BLKL does
         * not. */
        uint8_t* o = state.o;
        if (width > 3) {
            while (fit-- != 0) {
                *o++ = (uint8_t) fill;
                *o++ = (uint8_t) (fill >> 8);
                *o++ = (uint8_t) (fill >> 16);
                *o++ = (uint8_t) (fill >> 24);
            }
        } else {
            while (fit-- != 0) {
                *o++ = (uint8_t) f;
                if (width > 1) {
                    *o++ = (uint8_t) (f >> 8);
                }
                if (width > 2) {
                    *o++ = (uint8_t) (f >> 16);
                }
            }
        }
        state.o = o;
    }

    /* Nothing to do about a reservation still pending. The drop at the end of
     * the file only fires when the run ends exactly where the output does, and
     * writing these bytes has already moved that -- which is why `ds 3 / blkb
     * 3` is six bytes in the reference and `blkb 3 / ds 3` is three. */

    return true;
}

/* The quoted file name a file directive takes, copied somewhere it will
 * outlive the line it came from.
 *
 * The copy is not tidiness. `br_open` keeps the name, and `br_resume` reopens
 * by it when an include returns -- so a pointer into the reader's buffer would
 * be a pointer into whatever the *included* file refilled over it. The
 * destination is a local in the caller's frame, which lives exactly as long as
 * the reader that holds it.
 *
 * Double quotes only: the reference calls `INCLUDE 'x.inc'` and `INCLUDE x.inc`
 * both a "String format error", so a bare word is not a file name there. */
static bool file_name(const char** pp, const char* e,
                      char* out, int cap) {
    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (*p != '"') {
        state.err = ZAP_E_EXPECTED_QUOTED_FILE_NAME;

        return false;
    }
    p++;

    int n = 0;
    while (p < e && *p != '"' && *p != '\n') {
        if (n + 1 >= cap) {
            state.err = ZAP_E_FILE_NAME_TOO_LONG;

            return false;
        }
        out[n++] = *p++;
    }
    if (*p != '"') {
        state.err = ZAP_E_STRING_NOT_TERMINATED;

        return false;
    }
    if (n == 0) {
        state.err = ZAP_E_EXPECTED_FILE_NAME;

        return false;
    }
    out[n] = 0;
    *pp = p + 1;

    return true;
}

/* `INCBIN "file"`: the file's bytes, straight into the output.
 *
 * No reader, no lines, nothing to save and nothing to restore -- it is
 * fill_put with a file in place of the 0xFF. The size is known before a byte
 * is read, so the room is asked for once and the read goes directly to the
 * cursor rather than through a staging buffer. */
static bool incbin_file(const char* name) {
    const uint8_t fh = mos_fopen(name, FA_READ);
    if (fh == 0) {
        state.err = ZAP_E_CANNOT_OPEN_FILE;

        return false;
    }
    FIL* fil = mos_getfil(fh);
    if (fil == NULL) {
        mos_fclose(fh);
        state.err = ZAP_E_CANNOT_OPEN_FILE;

        return false;
    }
    /* The size in evalue, not int: objsize is a 32-bit DWORD, and into an int
     * on the eZ80 a file past eight megabytes arrived as zero or negative --
     * skipped in silence, or refused as out of memory. The bound before the
     * first byte, because the whole file has to fit under the ceiling and
     * because the chunks below are written straight to the cursor: the
     * per-chunk room check sees a window, not the file. In evalue for the
     * reason out_reserve gives. */
    const evalue size = fil->obj.objsize;
    if ((evalue) out_here() + size > OUT_TOTAL_MAX) {
        mos_fclose(fh);
        state.err = ZAP_E_OUTPUT_PAST_24_BIT_RANGE;

        return false;
    }
    int n = (int) size;
    /* Through the window rather than into it. The file may be larger than the
     * window is wide -- the corpus has a 150 KB one -- and the read still goes
     * straight to the cursor rather than through a staging buffer. */
    while (n > 0) {
        if (!out_reserve_n(1)) {
            mos_fclose(fh);

            return false;
        }
        const int room = (int) (state.lim - state.o);
        const int take = n < room ? n : room;
        if ((int) mos_fread(fh, (char*) state.o, (unsigned) take) != take) {
            mos_fclose(fh);
            state.err = ZAP_E_CANNOT_READ_FILE;

            return false;
        }
        state.o += take;
        n -= take;
    }
    mos_fclose(fh);

    return true;
}

/* `INCLUDE "file"`: the file's lines, assembled here.
 *
 * The reader lives in `zap_state` so that the line loop reaches it without an
 * indirection, so an include saves the reader it displaces in this function's
 * own frame -- which lives exactly as long as the included file does, as does
 * `name`, whose storage the caller owns for the same reason.
 *
 * The parent's file handle is closed while the child runs and reopened after,
 * because MOS has few of them. `br_resume` seeks back to where the parent's
 * reader had reached, so it carries on mid-file without noticing.
 *
 * Paths are opened exactly as written, which is what the reference does: an
 * INCLUDE inside `sub/a.inc` naming `b.inc` opens `./b.inc`, not
 * `sub/b.inc`. */
__attribute__((noinline))
static bool include_file(const char* name) {
    if (state.depth >= INCLUDE_MAXDEPTH) {
        /* A file that includes itself, most likely. Each level costs a frame
         * for this, one for the line loop and one for assemble_line, which is
         * 111 bytes on its own; the machine has no memory protection and would
         * simply stop. */
        state.err = ZAP_E_INCLUDES_NESTED_TOO_DEEPLY;

        return false;
    }

    const char* const saved_path = state.path;
    const int saved_line = state.line;

    /* Suspended *before* the copy is taken, and that order is the whole of it.
     * br_suspend closes the handle and zeroes `fh_`, and br_resume refuses a
     * reader whose `fh_` is not zero -- it reads that as "not suspended". A
     * copy made first carries the old handle back over the zero, so the resume
     * is refused and the handle it names has already been closed. */
    const bool was_file = !state.rd.mem_;
    if (was_file && !br_suspend(&state.rd)) {
        state.err = ZAP_E_CANNOT_SET_FILE_ASIDE;

        return false;
    }
    const buf_reader saved = state.rd;
    Z_SITE("include reader");
    if (br_open(&state.rd, name, INCLUDE_BUF_KB) == NULL) {
        state.rd = saved;
        if (was_file) {
            br_resume(&state.rd);
        }
        state.err = ZAP_E_CANNOT_OPEN_FILE;

        return false;
    }
    state.path = name;
    state.line = 0;
    state.depth++;

    /* A conditional belongs to the file it is written in, so the state is
     * saved and restored around an include: `IF 1 / INCLUDE "x.inc"` where
     * x.inc has an IF of its own is how a header switches on what its caller
     * set. Both halves of the rule are the reference's -- an IF left open at
     * the end of an included file is an error, and so is an ENDIF in one that
     * would close the caller's. */
    const bool saved_cond = state.in_cond;
    const bool saved_emit = state.cond_emit;
    state.in_cond = false;
    state.cond_emit = true;

    bool ok = run_lines();
    if (ok && state.in_cond) {
        state.err = ZAP_E_IF_LEFT_OPEN_AT_END_FILE;
        ok = false;
    }
    state.in_cond = saved_cond;
    state.cond_emit = saved_emit;
    state.line_mode = saved_emit ? LINE_ASSEMBLE : LINE_SKIP;

    /* The child's buffer goes back whether it worked or not; on the way out of
     * a failure the message has already been kept, and state.path still names the
     * file it happened in, which is what the report wants. */
    br_destroy(&state.rd);
    state.depth--;
    state.rd = saved;
    if (!ok) {
        /* `state.path` names this file and points into this frame, which goes
         * away as soon as this returns. The report happens after every frame
         * has unwound, so it is copied somewhere that outlives them. */
        if (state.path != state.errpath) {
            int i = 0;
            while (i + 1 < (int) sizeof(state.errpath) && state.path[i] != 0) {
                state.errpath[i] = state.path[i];
                i++;
            }
            state.errpath[i] = 0;
            state.path = state.errpath;
        }

        return false;
    }
    if (!br_resume(&state.rd)) {
        state.err = ZAP_E_CANNOT_REOPEN_FILE;

        return false;
    }
    state.path = saved_path;
    state.line = saved_line;

    return true;
}

/* The condition on an IF: an expression, and optionally `== expression`.
 *
 * `==` works only here. Outside an IF the reference calls it "Invalid list
 * format", and `!=`, `<` and `>` are "Illegal operator" everywhere -- so this
 * is the whole of its comparison vocabulary, and none of the corpus's eleven
 * conditional files uses even this one.
 *
 * The value has to be known now. A name that is defined later is "Unknown
 * identifier" in the reference too, which has a second pass and could have
 * waited: deciding what to assemble cannot be deferred by either of us.
 */
static bool cond_value(evalue* out, const char** pp, const char* e) {
    const char* p = *pp;
    fwd_reset(NULL);
    uint8_t fwdmask = 0;
    evalue lhs = 0;
    if (!expr_value(&lhs, &p, e, &fwdmask)) {
        return false;
    }
    if (expr_fwd != NULL) {
        state.err = ZAP_E_LABEL_DEFINED_ALREADY;

        return false;
    }

    while (p < e && is_space_ch(*p)) {
        p++;
    }
    if (p[0] == '=' && p[1] == '=') {
        p += 2;
        if (compat_ez80) {
            /* The reference does not compare: it evaluates the left side and
             * throws the rest of the line away, so `IF 0 == 0` is false there,
             * `IF 1 == 2` is true, and `IF 1 == nosuchname` assembles because
             * the name is never looked at.
             *
             * It is a quiet bug -- `IF version == 2` means `IF version` -- but
             * it decides which bytes come out, so `-ez80` reproduces it. */
            while (p < e && *p != '\n' && *p != ';') {
                p++;
            }
        } else {
            fwd_reset(NULL);
            evalue rhs = 0;
            if (!expr_value(&rhs, &p, e, &fwdmask)) {
                return false;
            }
            if (expr_fwd != NULL) {
                state.err = ZAP_E_LABEL_DEFINED_ALREADY;

                return false;
            }
            lhs = lhs == rhs;
        }
    }

    *out = lhs;
    *pp = p;

    return true;
}

/* Defined just below; cond_skip hands the conditional directives on to it. */
__attribute__((noinline))
/* INCLUDE and INCBIN, in a function of their own for the sake of the frame.
 *
 * The name buffer is 80 bytes and has to live as long as the file it opens --
 * `br_open` keeps the pointer and `br_resume` reads it back. In
 * directive_line's frame it would take that frame past the 128 bytes an `ix`
 * displacement reaches, so the other directives would pay a five-instruction
 * address computation for a buffer they never touch. */
__attribute__((noinline))
static bool file_directive(uint8_t kind, const char** pp, const char* e,
                           const char** stop) {
    char name[INCLUDE_NAME_MAX];
    if (!file_name(pp, e, name, (int) sizeof(name))) {
        return false;
    }
    *stop = *pp;

    return kind == DIR_INCBIN ? incbin_file(name) : include_file(name);
}

/* `ASCIZ`: the data list at one byte a value, and then a single zero. Not a
 * zero per string -- `asciz "ab", "cd"` is 61 62 63 64 00.
 *
 * Out of line and tested after the data list, so DB, DW and DL reach theirs on
 * the one comparison they always did. */
__attribute__((noinline))
static bool asciz_line(const char** pp, const char* e,
                       const char** stop) {
    if (!emit_data(1, pp, e)) {
        return false;
    }
    if (!out_reserve()) {
        return false;
    }
    *state.o++ = 0;
    *stop = *pp;

    return true;
}

/* A directive line, or a report that this was not one.
 *
 * Out of line, and reached only where the mnemonic lookup has already failed,
 * so an instruction pays nothing for any of it: not a test, not a table, not a
 * character. */
__attribute__((noinline))
bool directive_line(const char* s, int n, const char* p,
                           const char* e, const char** stop) {
    const uint8_t kind = directive_of(s, n);
    if (kind == DIR_NONE) {
        /* An instruction with a mode suffix, asked here rather than in front
         * of the directive dispatch: assemble_line ends at
         * `return directive_line(...)` when the mnemonic table says no, and a
         * directive still reaches its own dispatch in one call. */
        uint8_t suffix = 0;
        const insninfo* const insn = suffixed_mnemonic(s, n, &suffix);
        if (insn != NULL) {
            if (cpu_mask != CPU_EZ80) {
                /* A mode suffix says which of two address widths an
                 * instruction runs in, and neither the Z80 nor the Z180 has
                 * two, so the reference refuses all eight spellings under
                 * either.
                 *
                 * Asked here rather than in suffixed_mnemonic, which returns
                 * NULL for a token that is not a suffixed mnemonic at all --
                 * `read.next` may be a macro. Refusing there would report this
                 * as an unknown instruction and lose which fault it was. */
                state.err = ZAP_E_NO_MODE_SUFFIX_CPU;

                return false;
            }

            return suffixed_insn(insn, suffix, p, e, stop);
        }

        /* A macro, or nothing this understands. Looked up last, after the
         * mnemonic table and the directives, so nothing that is either pays
         * for the walk -- and a macro has to be defined before it is used,
         * which the reference requires too. */
        const macro* m = macro_at(s, n);
        if (m == NULL) {
            err_tok(s, n);
            state.err = ZAP_E_UNKNOWN_INSTRUCTION;

            return false;
        }

        return macro_expand(m, p, e, stop);
    }

    if (kind == DIR_MACRO) {
        if (!macro_begin(&p, e)) {
            return false;
        }
        *stop = p;

        return true;
    }
    if (kind == DIR_ENDMACRO) {
        /* Only ever reached outside a definition, since inside one the line
         * loop hands it to macro_capture instead. */
        state.err = ZAP_E_NO_MACRO_OPEN;

        return false;
    }

    if (kind >= DIR_IF) {
        if (kind == DIR_ENDIF || kind == DIR_ELSE) {
            if (!state.in_cond) {
                /* "Missing IF directive" there, and the same here. */
                state.err = ZAP_E_NO_IF_OPEN;

                return false;
            }
            if (kind == DIR_ENDIF) {
                state.in_cond = false;
                state.cond_emit = true;
            } else {
                /* ELSE toggles, and toggles again: `IF 1 / a / ELSE / b /
                 * ELSE / c / ENDIF` assembles a and c in the reference. */
                state.cond_emit = !state.cond_emit;
            }
            state.line_mode = state.cond_emit ? LINE_ASSEMBLE : LINE_SKIP;
            *stop = p;

            return true;
        }

        /* IF. Nesting is refused because the reference refuses it, and that is
         * what makes this a flag rather than a stack. */
        if (state.in_cond) {
            state.err = ZAP_E_CONDITIONALS_DO_NOT_NEST;

            return false;
        }
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        evalue cond = 0;
        if (!cond_value(&cond, &p, e)) {
            return false;
        }
        state.in_cond = true;
        state.cond_emit = cond != 0;
        state.line_mode = state.cond_emit ? LINE_ASSEMBLE : LINE_SKIP;
        *stop = p;

        return true;
    }

    if (kind == DIR_CPU) {
        /* A setting, and the whole of it is `cpu_mask`. See there.
         *
         * The mode follows the machine: neither the Z80 nor the Z180 has ADL,
         * so selecting either turns it off, which is what makes `ld hl, 0x1234`
         * three bytes under them and four in eZ80 mode. The reference does the
         * same. */
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        const char* const cs = p;
        while (p < e && name_ch(*p)) {
            p++;
        }
        const int cn = (int) (p - cs);
        if (cn == 4 && same_ci_full("ez80", cs, 4)) {
            cpu_mask = CPU_EZ80;
            state.adl = ZAP_ADL;
        } else if (cn == 3 && same_ci_full("z80", cs, 3)) {
            cpu_mask = CPU_Z80;
            state.adl = false;
        } else if (cn == 4 && same_ci_full("z180", cs, 4)) {
            cpu_mask = CPU_Z180;
            state.adl = false;
        } else {
            /* "Unsupported CPU type" there. The Z280 has a bit in the table
             * and no rows tagged with it, so it is not offered. */
            state.err = ZAP_E_UNSUPPORTED_CPU_TYPE;

            return false;
        }
        *stop = p;

        return true;
    }

    if (kind == DIR_ENDRELOCATE) {
        if (!state.reloc) {
            /* "Missing RELOCATE directive" there. */
            state.err = ZAP_E_NO_RELOCATE_OPEN;

            return false;
        }
        state.org = state.reloc_org;
        state.reloc = false;
        *stop = p;

        return true;
    }

    if (kind == DIR_ASSUME) {
        /* `ASSUME ADL=0` or `=1`, and nothing else: the reference calls any
         * other name an invalid operand and any other value an invalid ADL
         * mode. Spaces are allowed around the equals. */
        if (cpu_mask != CPU_EZ80) {
            /* "No ADL mode for CPU type" there, and for *either* value: the
             * Z80 and the Z180 have no ADL to select, so `ADL=0` is refused as
             * well, even though it names the mode they are already in. */
            state.err = ZAP_E_NO_ADL_MODE_CPU;

            return false;
        }
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (!dir_is(p, "adl", 3) || (cclass[(uint8_t) p[3]] & C_MNEM) != 0) {
            state.err = ZAP_E_EXPECTED_ADL;

            return false;
        }
        p += 3;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p != '=') {
            state.err = ZAP_E_EXPECTED_AFTER_ADL;

            return false;
        }
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        /* Read as a number, not as one character: the reference takes
         * `adl=01` and means one by it. Only the two values, though -- `adl=2`
         * is "Invalid ADL mode" there. */
        evalue mode = 0;
        const char* const after = lit_value(p, e, &mode);
        if (after != NULL) {
            p = after;
        } else {
            /* Not only a literal: `assume adl=one` with `one` an EQU appears
             * in the reference's own corpus. A label still ahead is refused by
             * both assemblers, because the mode decides the width of every
             * immediate below it and there is nothing sensible to do with a
             * value that is not known yet. */
            fwd_reset(NULL);
            uint8_t fwdmask = 0;
            if (!expr_value(&mode, &p, e, &fwdmask)) {
                return false;
            }
            if (expr_fwd != NULL) {
                state.err = ZAP_E_LABEL_DEFINED_ALREADY;

                return false;
            }
        }
        if (mode != 0 && mode != 1) {
            state.err = ZAP_E_ADL_0_OR_1;

            return false;
        }
        state.adl = mode == 1;
        *stop = p;

        return true;
    }

    if (kind >= DIR_INCLUDE) {
        return file_directive(kind, &p, e, stop);
    }

    if (kind <= DIR_DW32) {
        if (!emit_data(kind, &p, e)) {
            return false;
        }
        *stop = p;

        return true;
    }

    /* After the data list and not before it, so that DB, DW, DL and DW32 reach
     * theirs on the one comparison they always did. */
    if (kind == DIR_ASCIZ) {
        return asciz_line(&p, e, stop);
    }

    /* DS and ALIGN both take one value that has to be known now -- the
     * reference refuses a label still ahead of either, having no way to reserve
     * an amount it does not know yet. */
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    /* The evaluator's word, narrowed at each use below. A count, an address
     * and a fill byte are all the machine's, but they arrive through the
     * evaluator and one of them -- BLKL's fill -- is four bytes wide. See
     * evalue. */
    evalue value = 0;

    /* A plain number, read here rather than through the evaluator, for the
     * same reason a data item is: a count is a literal far more often than it
     * is anything else. `ORG $ + 8` and `ALIGN size*2` still go the long way.
     *
     * lit_value wants what ends the digit run to end the item, and for these
     * the item is the rest of the line -- which it already accepts, since a
     * newline or a remark ends an item too. `DS 3,1,2` is the one form where a
     * comma follows, and the arguments after the count are ignored, so
     * stopping at the comma is right there as well. */
    const char* const lit = lit_value(p, e, &value);
    if (lit != NULL) {
        p = lit;
    } else {
        fwd_reset(NULL);
        uint8_t fwdmask = 0;
        if (!expr_value(&value, &p, e, &fwdmask)) {
            return false;
        }
        if (expr_fwd != NULL) {
            state.err = ZAP_E_LABEL_DEFINED_ALREADY;

            return false;
        }
    }

    if (kind == DIR_DS) {
        /* Refused rather than reproduced, and the measurement is worth
         * keeping because the first look at it says the opposite.
         *
         * `ds -1` on its own assembles cleanly in the reference and writes
         * nothing -- a reservation at the end of a file is dropped, so the
         * negative count never has to mean anything. Put a single byte after
         * it and the same source writes a **4,294,967,299-byte** file: the
         * count is read as unsigned and the gap is filled when the file is
         * written out. `blkb -2`, which emits rather than reserves, is
         * 3.5 GB with nothing after it at all.
         *
         * On a 512 KB machine there is no byte sequence there worth agreeing
         * with, so this says so instead. Same position as division by zero. */
        if (value < 0) {
            state.err = ZAP_E_DS_POSITIVE_NUMBER;

            return false;
        }
        if (!fill_take(value)) {
            return false;
        }
        /* `DS 3,1,2` is three bytes in the reference: the arguments after the
         * count are taken and ignored. The first of them is *evaluated* and
         * then dropped, which is two visible things -- a value unlike the fill
         * byte is said, and a name that is never defined is an error, because
         * 2.3 does this from a fixup and an unresolved fixup is a failure.
         *
         * So an initializer naming a label ahead becomes a fixup of its own,
         * which writes nothing: see FIX_DSINIT. One that evaluates here is
         * answered here. */
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (p < e && *p == ',') {
            p++;
            while (p < e && is_space_ch(*p)) {
                p++;
            }
            const char* is = p;
            while (p < e && *p != '\n' && *p != ';' && *p != ',') {
                p++;
            }
            const char* ie = p;
            while (ie > is && is_space_ch(ie[-1])) {
                ie--;
            }
            if (ie > is) {
                evalue iv = 0;
                uint8_t imask = 0;
                const char* ip = is;
                fwd_reset(NULL);
                if (!expr_value(&iv, &ip, ie, &imask)) {
                    return false;
                }
                if (expr_fwd != NULL) {
                    const sym* tgt;
                    const sym* isub;
                    bool ineg;
                    if (!fwd_result(&tgt, &isub, &ineg)) {
                        return false;
                    }
                    /* The fill byte rides in `off`, which this kind does not
                     * use for a site. */
                    if (!fix_add(tgt, isub, 0, FIX_DSINIT, (int) state.fill)) {
                        return false;
                    }
                } else if (iv != (evalue) state.fill) {
                    warn_initializer(is, (int) (ie - is));
                }
            }
        }
        while (p < e && *p != '\n' && *p != ';') {
            p++;
        }
        *stop = p;

        return true;
    }

    if (kind == DIR_FILLBYTE) {
        /* One byte, and it stands for the rest of the assembly and no part of
         * what is behind it. A run already written keeps what it was written
         * with; anything still reserved has not been written yet, so it will
         * take this when it is, and needs nothing done to it here.
         *
         * 2.2 reached backwards -- the runs above a file's first FILLBYTE took
         * its *last* value, because `fillbyte` survived into the second pass
         * and the gaps were filled there. 2.3 has no second pass and neither
         * has zap; the machinery that reproduced it is gone. */
        state.fill = (uint8_t) value;
        *stop = p;

        return true;
    }

    if (kind == DIR_RELOCATE) {
        if (state.reloc) {
            /* "Nested relocate not allowed" there. */
            state.err = ZAP_E_RELOCATE_DOES_NOT_NEST;

            return false;
        }

        if (value < 0 || value > 0xFFFFFF) {
            /* "Address outside 24-bit range" there, and the eZ80's address
             * space is exactly that, so this is the reference being right
             * rather than a quirk to reproduce. `$1000000` is one past it and
             * `-1` is the other end. */
            state.err = ZAP_E_ADDRESS_OUTSIDE_24_BIT_RANGE;

            return false;
        }

        /* The bytes stay where they are and the addresses move. `org` is what
         * every address is measured from -- a label, `$`, an EQU taking `$`,
         * and the target of every fixup -- so displacing it is the whole of
         * this directive and nothing else has to know. */
        state.reloc_org = state.org;
        state.reloc = true;
        state.org = (int) value - out_here();
        *stop = p;

        return true;
    }

    if (kind >= DIR_BLKB) {
        /* Everything at or above DIR_INCLUDE has returned by here, so this is
         * BLKB, BLKW, BLKP or BLKL, and the width follows from which. */
        if (value < 0) {
            /* Refused for the reason DS is: the reference reads the count as
             * unsigned, so a negative one is sixteen megabytes of fill and a
             * successful assembly. On a 512 KB machine that is a way to lose
             * the program rather than a feature. */
            state.err = ZAP_E_BLK_POSITIVE_NUMBER;

            return false;
        }
        const int width = kind - DIR_BLKB + 1;

        /* The fill, if one is given. FILLBYTE's value at the unit width if
         * not, which is 0xFF until something says otherwise. */
        evalue fill = state.fill;
        const sym* pending = NULL;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p == ',') {
            p++;
            while (p < e && is_space_ch(*p)) {
                p++;
            }
            const char* const flit = lit_value(p, e, &fill);
            if (flit != NULL) {
                p = flit;
            } else {
                const char* const fs = p;
                fwd_reset(NULL);
                uint8_t fwdmask = 0;
                if (!expr_value(&fill, &p, e, &fwdmask)) {
                    return false;
                }
                if (expr_fwd != NULL || expr_fwd_bad) {
                    /* The fill names something ahead. It is one value repeated
                     * n times, so there is nothing a per-byte fixup could
                     * usefully do; the run is written now and filled in when
                     * the value is known, which is one record however long it
                     * is. The count is a different matter and is still
                     * refused: how many bytes there are decides where
                     * everything after them lands. */
                    pending = defer_text(fs, (int) (p - fs));
                    if (pending == NULL) {
                        return false;
                    }
                    fill = 0;
                }
            }
        }
        if (pending != NULL && value > 0) {
            if (state.fillp_used == state.fillp_cap) {
                Z_SITE("deferred fills");
                const int want = state.fillp_cap == 0 ? 4 : state.fillp_cap + state.fillp_cap;
                fillpatch* grown =
                    (fillpatch*) realloc(state.fillp, (size_t) want * sizeof(fillpatch));
                if (grown == NULL) {
                    state.err = ZAP_E_OUT_MEMORY_LABELS;

                    return false;
                }
                state.fillp = grown;
                state.fillp_cap = want;
            }
            fillpatch* fp = &state.fillp[state.fillp_used++];
            fp->sp = pending;
            fp->off = out_here();
            fp->count = value;
            fp->width = (uint8_t) width;
            fp->line = state.line;
        }
        if (!emit_block(value, width, fill)) {
            return false;
        }

        /* Anything after the fill is taken and ignored, as it is for DS. */
        while (p < e && *p != '\n' && *p != ';') {
            p++;
        }
        *stop = p;

        return true;
    }

    if (kind == DIR_ORG) {
        /* Out of ADL mode an address is two bytes, and the reference refuses
         * one that is not -- "Address outside 16-bit range". Only here: it
         * does not check ORG against the 24-bit ceiling in ADL mode, and it
         * does not check RELOCATE against 16 bits out of it, so neither does
         * this. Rare enough that a compare costs nothing measurable, and it is
         * on the directive path rather than the instruction path anyway. */
        if (!state.adl && value > 0xFFFF) {
            state.err = ZAP_E_ADDRESS_OUTSIDE_16_BIT_RANGE;

            return false;
        }

        /* The first ORG in a file moves the origin; every later one pads out
         * to its address. That is not a guess -- two ORGs with nothing between
         * them write the 64 KB gap in the reference, so the second is already
         * behaving as a pad even though nothing has been emitted. */
        if (!state.org_set && out_here() == 0) {
            state.org = value;
        } else {
            const int here = state.org + out_here();
            if (value < here) {
                /* "New address lower than current PC address" there, and the
                 * same here: an ORG that goes backwards would have to unwrite
                 * bytes that are already placed. */
                state.err = ZAP_E_ORG_GOES_BACKWARDS;

                return false;
            }
            /* Whatever was reserved above this ORG is written first, at the
             * value in force now -- the pad is a write, and a write is what
             * settles a reservation.
             *
             * Then the pad itself, written rather than reserved. The reference
             * puts it down where it stands: it survives at the end of a file
             * where a DS is dropped, and a later FILLBYTE does not reach back
             * to it. `fillbyte 0x11 / org $+4 / fillbyte 0xAA` is four 0x11
             * there, where the same shape with DS gives 0xAA. */
            if (!out_settle() || !fill_put(value - here)) {
                return false;
            }
        }
        state.org_set = true;
        *stop = p;

        return true;
    }

    if (value <= 0) {
        state.err = ZAP_E_ALIGN_POSITIVE_NUMBER;

        return false;
    }
    if ((value & (value - 1)) != 0) {
        state.err = ZAP_E_ALIGN_POWER_TWO;

        return false;
    }
    /* Pad to the next multiple. `-addr & (n - 1)` is the distance to it, and
     * the AND is a call to __iand on a 24-bit value -- once per ALIGN, which
     * is a price a directive can pay. */
    const int addr = state.org + out_here();
    if (!fill_take((-addr) & (value - 1))) {
        return false;
    }
    *stop = p;

    return true;
}
