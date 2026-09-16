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

__attribute__((noinline)) bool assemble_line(const char* p, const char* e, const char** stop) {
    /* Bounded, like every character scan in the file. Every line of the source
     * starts here, so the generated assembly is worth re-reading whenever this
     * function changes: `dec iy` before a loop head is the sign of a rotated
     * scan, and test/run.sh counts them. */
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
    TRUNC_AT(1, *p);

    const char* s = p;
    while (p < e && (cclass[(uint8_t) *p] & C_MNEM) != 0) {
        p++;
    }
    int n = (int) (p - s);
    if (n == 0) {
        state.err = ZAP_E_EXPECTED_INSTRUCTION;

        return false;
    }
    TRUNC_AT(2, n);

    /* A line that is not simply assembled: a conditional is switched off, or a
     * macro is being defined and this belongs to its body. One field and one
     * branch to find out, tested after the token and before the label --
     * neither a switched-off branch nor a macro body may define one, and the
     * reference defines neither. */
    if (state.line_mode != LINE_ASSEMBLE) {
        return state.line_mode == LINE_CAPTURE
                   ? macro_capture(s, n, p, e, stop)
                   : cond_skip(s, n, p, e, stop);
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
        LTRUNC_AT(1);
        const int addr = state.org + out_here();
        /* "Label too long" at sixty-five characters, as in the reference. The
         * `@` of a local counts towards the limit, which is why this is asked
         * once for both rather than after the two are told apart.
         *
         * Unsigned, because a length cannot be negative and a signed compare
         * is a `call pe, __setflag` -- which the codegen budget in test/run.sh
         * counts. */
        if ((unsigned) n > LABEL_MAX) {
            state.err = ZAP_E_LABEL_TOO_LONG;

            return false;
        }
        if (*s == '@') {
            /* `@@` is an anonymous label, not a local: it has no name to
             * collide with, so writing it twice is not a redefinition. */
            if (n == 2 && s[1] == '@') {
                if (state.expanding != 0) {
                    /* "No anonymous labels allowed in macro definition"
                     * there, and refused at the invocation rather than at the
                     * definition, exactly as a global label in a body is. */
                    state.err = ZAP_E_NO_ANONYMOUS_LABELS_ALLOWED_IN_MAC;

                    return false;
                }
                if (!anon_define(addr)) {
                    return false;
                }
            } else if (loc_define(s, n, addr) == NULL) {
                /* A local. It is not tested against the number formats: the
                 * reference returns before that check for a local, so `@123:`
                 * and `@0ffh:` are labels there and have to be here. */
                return false;
            }
        } else {
            if (numeric_token(s, n)) {
                state.err = ZAP_E_INVALID_LABEL;

                return false;
            }
            if (state.expanding != 0) {
                state.err = ZAP_E_NO_GLOBAL_LABELS_ALLOWED_IN_MACRO;

                return false;
            }
            if (sym_define(s, n, addr) == NULL) {
                return false;
            }
            /* A global ends the scope before it starts a new one -- but not
             * until this line is done with, because the rest of it still
             * belongs to the scope being closed. */
            state.scope_line = state.line;
        }
        LTRUNC_AT(2);
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        *stop = p;
        if (p >= e || *p == '\n' || *p == ';') {
            return true;
        }

        /* `name: EQU value`, which names a value rather than an address.
         *
         * Tested after the return above rather than before the definition, so
         * that a bare `label:` line -- which never reaches here -- pays
         * nothing for it.
         *
         * The label has already been defined at the address the line was at,
         * and equ_line replaces that with the value. It re-finds the symbol by
         * name rather than being handed it, because keeping that pointer in a
         * local here would cost three more bytes of frame, which is enough to
         * lose `iy` as the line pointer for every line in the file. One hash
         * lookup per EQU is the cheaper end of that trade. */
        if (is_equ_at(p)) {
            return equ_line(s, n, p, e, stop);
        }

        s = p;
        while (p < e && (cclass[(uint8_t) *p] & C_MNEM) != 0) {
            p++;
        }
        n = (int) (p - s);
        if (n == 0) {
            state.err = ZAP_E_EXPECTED_INSTRUCTION;

            return false;
        }
    }

    TRUNC_AT(3, n);

    const insninfo* insn = mnemonic_of(s, n);
#ifdef TRUNC_NODIR
    /* Stage 4 without the directive path, so the two halves of that stage can
     * be told apart: the lookup happens on every line, the dispatch on one in
     * eight. */
    TRUNC_AT(4, insn != NULL);
#endif
    if (insn == NULL) {
        /* A directive, a macro, a suffixed instruction, or nothing this
         * understands. All of them are asked here and nowhere earlier, so an
         * instruction line never tests for any of them.
         *
         * This must stay a *tail* call. Written as `insn = something(...)`
         * with a fall-through into the operand parsing below, everything this
         * function holds would have to survive the call -- which is paid on
         * every line, including the ones with no directive in them. Nothing
         * returns to this point. */
        return directive_line(s, n, p, e, stop);
    }

    TRUNC_AT(4, insn != NULL);

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
        /* A third operand, which only RES and SET have. See third_operand.
         *
         * One compare, on a character already in a register: parse_operand
         * leaves the cursor past whatever follows the operand on every exit,
         * so there is no scan here and a two-operand line pays a byte compare
         * and nothing else. */
        if (*p == ',') {
            return third_operand(insn, &a, &b, p + 1, e, stop);
        }
    } else {
        b = dop_none;
    }

    *stop = p;
    TRUNC_AT(5, a.mode + b.mode + a.r0 + b.r0);

    const isa_row* row = match_row(insn, &a, &b);
    if (row == NULL) {
        err_tok(insn->name, insn->len);
        state.err = ZAP_E_NO_SUCH_INSTRUCTION_FORM;

        return false;
    }

    TRUNC_AT(6, row != NULL);

    return emit_row(row, &a, &b, 0);

#ifdef TRUNC
trunc_done:
    /* The loop wants the line left on its newline. In the seventh build this
     * runs too, over the handful of characters an instruction leaves behind,
     * so every variant carries it and it cancels out of the differences. */
    while (p < e && *p != '\n') {
        p++;
    }
    *stop = p;

    return true;
#endif
}

/* Patches every forward reference, once the whole source has been read.
 *
 * This is what buys the single pass. zap never looks at a line twice, so a
 * reference to a label defined later cannot be resolved where it is read; the
 * output is held in memory in full, so it is patched here instead. The cost of
 * labels is then a line item -- this loop and the table behind it -- rather
 * than a second pass over the source, which would be most of the program
 * again.
 *
 * Little-endian, as everywhere else here. */
/* Settles every expression that was kept as text, before a byte is patched.
 *
 * Everything is defined by now, so the evaluator is simply run again over the
 * copy. A forward reference that survives even this is one to a name nothing
 * ever defined, and is reported against the line that wrote it. */
static bool resolve_deferred(void) {
    for (int i = 0; i < state.defer_used; i++) {
        defexpr* d = &state.defer[i];
        const char* p = d->text;
        evalue v = 0;
        uint8_t mask = 0;
        fwd_reset(NULL);
        state.line = d->line;
        if (!expr_value(&v, &p, d->text + d->len, &mask)) {
            return false;
        }
        if (expr_fwd != NULL || expr_fwd_bad) {
            state.err = ZAP_E_UNKNOWN_LABEL;

            return false;
        }
        d->sp->addr = v;
        d->sp->defined = true;
    }

    return true;
}

/* Checks the blocks whose fill value was not known when they were written.
 *
 * The bytes themselves are written by the sweep, with everything else; this is
 * only the part that has to happen here, where the line that wrote the block
 * is still the line a failure is reported against. */
static bool resolve_fills(void) {
    for (int i = 0; i < state.fillp_used; i++) {
        const fillpatch* fp = &state.fillp[i];
        if (!fp->sp->defined) {
            state.line = fp->line;
            state.err = ZAP_E_UNKNOWN_LABEL;

            return false;
        }
    }

    return true;
}

/* Whether the runs reserved before the file's first FILLBYTE need anything.
 *
 * They were written with 0xFF, and the reference gives them the file's *last*
 * FILLBYTE. If there was none, or it was 0xFF anyway, what is already there is
 * right -- which is the case for every source that never says FILLBYTE. */
static bool early_fills_pending(void) {
    return state.fill_seen && state.fill != 0xFF && state.earlyf_used > 0;
}

/* Writes into `buf`, which holds the output over [lo, hi), everything still
 * owed to that range: the point patches left behind by the window, the blocks
 * whose fill was a forward reference, and the runs waiting on the last
 * FILLBYTE. Each is clipped to the range, because a patch or a run may straddle
 * the edge of it. */
/* One ascending run of late patches, over [from, to). */
static void apply_late(uint8_t* buf, int lo, int hi, int from, int to, int* cur) {
    for (int i = *cur < from ? from : *cur; i < to; i++) {
        const latepatch* lp = &state.late[i];
        if (lp->off >= hi) {
            break;
        }
        const int n = lp->kind == LATE_OR ? 1 : lp->kind;
        if (lp->off + n <= lo) {
            /* Wholly behind this chunk, and so is everything before it in this
             * run: the next chunk can start here. */
            *cur = i + 1;
            continue;
        }
        if (lp->kind == LATE_OR) {
            buf[lp->off - lo] |= lp->b[0];
            continue;
        }
        for (int k = 0; k < n; k++) {
            const int at = lp->off + k;
            if (at >= lo && at < hi) {
                buf[at - lo] = lp->b[k];
            }
        }
    }
}

static void apply_range(uint8_t* buf, int lo, int hi, int* cur) {
    apply_late(buf, lo, hi, 0, state.late_split, &cur[0]);
    apply_late(buf, lo, hi, state.late_split, state.late_used, &cur[3]);

    for (int i = cur[1]; i < state.fillp_used; i++) {
        const fillpatch* fp = &state.fillp[i];
        const int n = fp->count * fp->width;
        if (fp->off >= hi) {
            break;
        }
        if (fp->off + n <= lo) {
            cur[1] = i + 1;
            continue;
        }
        const evalue v = fp->sp->addr;
        for (int k = 0; k < n; k++) {
            const int at = fp->off + k;
            if (at >= lo && at < hi) {
                buf[at - lo] = (uint8_t) (v >> (8 * (k % fp->width)));
            }
        }
    }

    if (early_fills_pending()) {
        for (int i = cur[2]; i < state.earlyf_used; i++) {
            const fillrun* r = &state.earlyf[i];
            if (r->off >= hi) {
                break;
            }
            if (r->off + r->count <= lo) {
                cur[2] = i + 1;
                continue;
            }
            int from = r->off < lo ? lo : r->off;
            int to = r->off + r->count > hi ? hi : r->off + r->count;
            memset(buf + (from - lo), state.fill, (size_t) (to - from));
        }
    }
}

/* Applies everything the window left behind, in one pass up the file.
 *
 * Ascending, and in chunks the size of the window, because of the filesystem
 * this runs on: FF_FS_TINY means a FIL has no sector buffer of its own and
 * every partial write is a read-modify-write through the one the FAT is also
 * using, and FF_USE_FASTSEEK is off, so a seek walks the cluster chain -- from
 * the current position when it goes forward, from the start when it goes back.
 * A seek and a write per patch would be two sector transfers each and a chain
 * walk between them. This is two transfers per *chunk*, and a chunk with
 * nothing owed to it costs one forward seek and no transfer at all.
 *
 * When the output never outgrew the window there is no file, and the whole of
 * it is one chunk that is already in memory. */
static bool resolve_late(void) {
    if (state.late_used == 0 && state.fillp_used == 0 && !early_fills_pending()) {
        return true;
    }
    const int total = out_here();
    /* Where each list has got to. The lists ascend and so do the chunks, so a
     * patch is looked at once rather than once per chunk -- which matters at a
     * small window, where there are many chunks and the same thousands of
     * patches. */
    int cur[4] = {0, 0, 0, state.late_split};
    if (state.out_fh == 0) {
        apply_range(state.win, 0, total, cur);

        return true;
    }
    for (int base = 0; base < total; base += state.cap) {
        int n = total - base;
        if (n > state.cap) {
            n = state.cap;
        }
        if (mos_flseek(state.out_fh, (uint32_t) base) != 0
            || (int) mos_fread(state.out_fh, (char*) state.win, (uint24_t) n) != n) {
            state.err = ZAP_E_CANNOT_READ_OUTPUT;

            return false;
        }
        apply_range(state.win, base, base + n, cur);
        if (mos_flseek(state.out_fh, (uint32_t) base) != 0
            || (int) mos_fwrite(state.out_fh, (char*) state.win, (uint24_t) n) != n) {
            state.err = ZAP_E_CANNOT_WRITE_OUTPUT;

            return false;
        }
    }

    return true;
}

static bool resolve_fixups(void) {
    if (!resolve_deferred() || !resolve_fills()) {
        return false;
    }
    /* Everything recorded from here is a global fixup, and the globals start
     * again at the top of the file. See late_split. */
    state.late_split = state.late_used;
    for (int i = 0; i < state.fix_used; i++) {
        if (!patch_fixup(&state.fixups[i])) {
            return false;
        }
    }

    return true;
}

/* The loop over one file's lines.
 *
 * A function of its own because INCLUDE re-enters it: an included file runs
 * this same loop over its own reader and returns here when it ends. `p` and
 * `end` are locals, so the only thing an include has to save is the reader,
 * which it does in its own frame.
 *
 * The alternative -- a stack of readers in `zap_state`, popped when a file ends --
 * would put a test for "is there a parent file" in the hottest loop in the
 * assembler, to answer a question only an INCLUDE can ask.
 */
__attribute__((noinline)) bool run_lines(void) {
    /* The cursor is a pointer, not an offset into the buffer. An offset would
     * be turned into a pointer on the way into every line and back again on
     * the way out, for a value only this loop uses. br_fill_lines never reads
     * `bpos_`; it only resets it. */
    const char* p = state.rd.buf_;

    /* Empty for a file, so the first pass through the loop fills it. A reader
     * over memory -- which is what a macro expansion is -- has the whole of its
     * content already and can never refill: `br_fill_lines` says so and returns
     * false, which the loop reads as end of file. Without this an expansion
     * assembles to nothing at all, quietly. */
    const char* end = state.rd.mem_ ? p + state.rd.bsz_ : p;

    while (true) {
        buf_reader* r = &state.rd;
        if (p >= end) {
            if (!line_fill(r)) {
                if (state.err != ZAP_OK) {
                    return false;
                }

                break;
            }
            p = r->buf_;
            end = p + r->bsz_;
        }

        /* The buffer holds whole lines, so this one's newline is in it. */
        const char* stop = p;

        /* Where the output stood before this line, for the listing. Held in
         * `zap_state` rather than in locals, which would be live across assemble_line
         * and take two registers from the loop that has fewest to spare. At a
         * fixed address the stores are absolute. */
        if (listing) {
            state.lst_off = out_here();
            state.lst_pc = state.org + out_here();
            state.lst_p = p;
        }

        state.line++;
        if (!assemble_line(p, end, &stop)) {
            /* The innermost failure has already taken `errline`, so this line
             * is the one that invoked it, and its text is the one thing the
             * expansion could not record for itself. Which file and which line
             * it was, it did record -- see macro_expand. */
            if (!state.errhave && !state.err_elsewhere) {
                err_line(state.errline, p, end);
                state.errhave = true;
            } else if (state.errfrompath != NULL && state.errfrom[0] == 0) {
                err_line(state.errfrom, p, end);
            }

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
            /* Walked with a local, and `stop` written once at the end.
             *
             * `stop` has had its address taken -- assemble_line reports
             * through it -- so the compiler cannot keep it in a register and
             * would store it to the frame on every character of every comment.
             * A local that is never addressed stays in a register, and
             * comments are a quarter of the bytes in a real program. */
            const char* q = stop;
            while (q < end && is_space_ch(*q)) {
                q++;
            }
            if (*q == ';') {
                /* A remark after the instruction. Its body is never looked at
                 * -- the search for the newline here walks it once and that
                 * is all a comment ever costs. */
                while (q < end && *q != '\n') {
                    q++;
                }
            } else if (*q != '\n') {
                state.err = ZAP_E_UNEXPECTED_TEXT_AFTER_INSTRUCTION;

                return false;
            }
            stop = q;
        }
        /* The reference takes 256 characters of line and refuses the 257th --
         * counting a CR, so a CRLF file gets 255 of them. zap's own limit is
         * the 16 KB reader buffer, which meant a line that assembled here
         * would not assemble there, on a file neither of us should take.
         *
         * Asked here, where the line is already walked to its end, rather
         * than by scanning ahead for the newline: `stop` is the newline and
         * the length is a subtract. The reference refuses the line before
         * reading it and this refuses it after, so a long line that is also
         * malformed reports the other fault first. Both refuse the file,
         * which is what a source can observe. */
        if ((int) (stop - p) > LINE_MAX_CHARS) {
            state.err = ZAP_E_LINE_TOO_LONG;

            return false;
        }

        if (listing) {
            if (!state.lst_done) {
                /* Ending where the writing ended, not where the next byte
                 * would go: a line that only reserves space wrote nothing, and
                 * lists nothing. */
                list_line(state.lst_pc, state.lst_off, out_written(), state.line,
                          0, p, stop);
                if (state.fix_touched) {
                    lstfix_add(state.lst_off, out_written());
                }
            }
            state.lst_done = false;
            state.fix_touched = false;
        }

        /* A line that was only a remark stops at the semicolon, so the rest
         * of it is walked here. This is the whole cost of a comment: one pass
         * over its bytes, looking for the newline and nothing else. */
        /* `stop` is on the newline: every other case returned above, and the
         * comment skip before it ends on one too. */
        /* No bound on the step. A line always ends on a newline inside the
         * buffer, or on the sentinel one past it, so `stop + 1` is at worst
         * one past the end -- and the refill above tests `p >= end`, which
         * that satisfies. A bounded step would be a 24-bit compare on every
         * line. */
        p = stop + 1;
    }


    return true;
}

__attribute__((noinline)) static bool run(const char* path) {
    Z_SITE("source reader");
    if (br_open(&state.rd, path, BUF_KB) == NULL) {
        state.err = ZAP_E_CANNOT_OPEN_SOURCE;

        return false;
    }

    /* One window, allocated once and never grown. What the source is worth in
     * output no longer decides anything: the buffer is a view on a file that
     * is written as it fills. */
    state.cap = OUT_WINDOW;
    Z_SITE("output buffer");
    state.win = (uint8_t*) malloc((size_t) state.cap);
    if (state.win == NULL) {
        state.err = ZAP_E_OUT_MEMORY;

        return false;
    }
    state.o = state.win;
    /* The buffer holds the whole output, so its first byte is the output's. */
    state.wbase = 0;
    state.org = opt_org;
    state.org_set = false;
    state.fill = opt_fill;
    state.reloc = false;
    state.reloc_org = 0;
    state.adl = opt_adl;
    /* Reset with the rest, and it has to be: `cpu_mask` is a file-scope
     * static so that match_row need not carry it, and the unit tests assemble
     * many sources in one process. Without this, one `.cpu Z80` would decide
     * what every later test in the same run could encode. */
    cpu_mask = CPU_EZ80;
    state.in_cond = false;
    state.cond_emit = true;
    state.line_mode = LINE_ASSEMBLE;
    state.macros = NULL;
    state.defining = NULL;
    state.expanding = 0;
    state.undo = NULL;
    state.undo_used = 0;
    state.undo_cap = 0;
    state.subfix = NULL;
    state.subfix_used = 0;
    state.subfix_cap = 0;
    state.fix_settled = 0;
    state.fix_peak = 0;
    state.err_elsewhere = false;
    state.defer = NULL;
    state.defer_used = 0;
    state.defer_cap = 0;
    state.fillp = NULL;
    state.fillp_used = 0;
    state.fillp_cap = 0;
    state.earlyf = NULL;
    state.earlyf_used = 0;
    state.earlyf_cap = 0;
    state.fill_seen = false;
    state.pend = 0;
    state.late = NULL;
    state.late_used = 0;
    state.late_cap = 0;
    state.late_split = 0;
    state.out_fh = 0;
    for (int i = 0; i < INCLUDE_MAXDEPTH; i++) {
        state.expbuf[i] = NULL;
        state.expcap[i] = 0;
    }
    state.lim = state.win + state.cap - OUT_MAX_INSN;
    Z_SITE("symbol buckets");
    state.syms = (symslot*) calloc(NSYMB, sizeof(symslot));
    if (state.syms == NULL) {
        state.err = ZAP_E_OUT_MEMORY;

        return false;
    }
    /* One block up front, so sym_define never has to ask whether there is
     * one; it only ever asks whether the newest is full. */
    Z_SITE("symbol blocks");
    state.blocks = (symblock*) malloc(sizeof(symblock));
    if (state.blocks == NULL) {
        state.err = ZAP_E_OUT_MEMORY;

        return false;
    }
    state.blocks->next = NULL;
    state.syms_used = 0;
    state.line = 0;
    state.path = path;
    state.depth = 0;

    if (!run_lines()) {
        return false;
    }

    /* The last scope ends with the source, and settles the same way any other
     * one does. Before the globals, because a local that was never defined
     * should be reported against the line that used it rather than after a
     * global's failure somewhere else. */
    if (state.defining != NULL) {
        /* "Unfinished macro definition" there, and the same here: a body that
         * never closes has swallowed the rest of the file. */
        state.err = ZAP_E_MACRO_WAS_NEVER_CLOSED;

        return false;
    }

    if (state.in_cond) {
        /* "Missing ENDIF directive" there, and the same here: a conditional
         * that never closes has silently dropped whatever followed it. */
        state.err = ZAP_E_IF_WAS_NEVER_CLOSED;

        return false;
    }

    if (!scope_end()) {
        return false;
    }

    if (!resolve_fixups()) {
        return false;
    }

    /* Space that was reserved and never written over is not output: `DS 4` at
     * the end of a file produces nothing, and neither does a trailing `ALIGN`,
     * as in the reference. Nothing has to be taken back -- it was never
     * written -- but out_here() counts it, and from here on that number is the
     * length of the file. */
    state.pend = 0;

    return true;
}

/* Everything run() may have allocated, freed in one place so that the two
 * error paths and the success path cannot drift apart. */
static void dz_free(void) {
    free(state.win);
    free(state.syms);
    free(state.fixups);
    free(state.lfixups);
    free(state.undo);
    free(state.subfix);
    free(state.defer);
    free(state.fillp);
    free(state.earlyf);
    free(state.late);
    free(state.lstfix);
    for (int i = 0; i < INCLUDE_MAXDEPTH; i++) {
        free(state.expbuf[i]);
    }
    while (state.macros != NULL) {
        macro* next = state.macros->next;
        free(state.macros->body);
        free(state.macros->marks);
        free(state.macros);
        state.macros = next;
    }
    while (state.names != NULL) {
        namblock* next = state.names->next;
        free(state.names);
        state.names = next;
    }
    /* The local name blocks are rewound rather than freed at the end of a
     * scope, so `locnames` may be pointing part way down a list that is still
     * whole. Freed from `locnamfirst`, which is the head: locnam_take links
     * each new block on at the end so that this walk reaches all of them and
     * the rewind can fill them again. */
    while (state.locnamfirst != NULL) {
        namblock* next = state.locnamfirst->next;
        free(state.locnamfirst);
        state.locnamfirst = next;
    }
    while (state.blocks != NULL) {
        symblock* next = state.blocks->next;
        free(state.blocks);
        state.blocks = next;
    }
    while (state.locfirst != NULL) {
        locblock* next = state.locfirst->next;
        free(state.locfirst);
        state.locfirst = next;
    }
    br_destroy(&state.rd);
}

/* `-ez80`, spelled out rather than compared with same_ci.
 *
 * same_ci is the mnemonic compare, and it has to stay inlined into mnemonic_of,
 * which runs on every line of the source. One cold caller here -- once, at
 * startup, on an argument -- is enough for the compiler to stop inlining it
 * everywhere, turning the hot compare into a call per candidate. A
 * `static inline` helper is inlined at the compiler's discretion, and a single
 * cold caller can take that away from every hot one. */
static bool is_ez80_opt(const char* a) {
    return a[0] == '-' && (a[1] | 0x20) == 'e' && (a[2] | 0x20) == 'z'
           && a[3] == '8' && a[4] == '0' && a[5] == 0;
}

/* A hexadecimal value on an option, attached or in the next argument.
 *
 * The reference takes both -- `-o50000` and `-o 50000` are the same thing --
 * so a command line written for it works here unaltered. Returns false for a
 * value that is not hexadecimal at all, which is worth saying rather than
 * quietly assembling at an address nobody asked for. */
static bool opt_hex(const char* attached, const char* next, int* used,
                    int* out) {
    const char* p = attached;
    if (*p == 0) {
        if (next == NULL) {
            return false;
        }
        p = next;
        *used = 1;
    }
    /* Read here rather than through hexval, which is filled by build_cclass
     * -- and build_cclass runs after the arguments are parsed, so at this
     * point that table is still all zeros. */
    int v = 0;
    int n = 0;
    for (; *p != 0; p++, n++) {
        int d;
        if (*p >= '0' && *p <= '9') {
            d = *p - '0';
        } else if ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f') {
            d = (*p | 0x20) - 'a' + 10;
        } else {
            return false;
        }
        v = (v << 4) | d;
    }
    if (n == 0) {
        return false;
    }
    *out = v;

    return true;
}

/* One flag, taken from anywhere on the line so that `zap -ez80 a.s a.bin` and
 * `zap a.s a.bin -ez80` both work; the reference accepts its own options
 * either side of the filenames and this is meant to drop in.
 *
 * OUT OF LINE, AND NOT FOR TIDINESS. `run` is inlined into main, so main holds
 * the loop over the source lines, and two hundred instructions of argument
 * handling in front of that loop move its register allocation -- which costs
 * real time on a build where the option is never even given. Code that is
 * merely *there* is not free. */
static void usage(void) {
    printf("Usage: zap <filename> [output filename] [OPTION]\r\n\r\n");
    printf("  -v\tList version information only\r\n");
    printf("  -h\tList help information\r\n");
    printf("  -o\tOrg start address in hexadecimal format, default is 040000\r\n");
    printf("  -b\tFillbyte in hexadecimal format, default is FF\r\n");
    printf("  -a\tADL mode 1/0, default is 1\r\n");
    printf("  -w\tWarn about truncated values\r\n");
    printf("  -i\tIgnore value truncation warnings, which is the default\r\n");
    printf("  -l\tListing to file with .lst extension\r\n");
    printf("  -s\tExport symbols\r\n");
    printf("  -d\tDirect listing to console\r\n");
    printf("  -c\tNo color codes in output\r\n");
    printf("  -x\tDisplay assembly statistics\r\n");
    printf("  -ez80\tThe reference assembler's expression rules\r\n");
}

/* The reference's options, taken by the same letters and in the same forms.
 *
 * A drop-in replacement that needs the command line rewritten is not one, so
 * every flag it has is accepted here -- three of them change the bytes and
 * have to be implemented, two are recognised and do nothing, and the rest do
 * what they say. One flag is zap's own, which is `-w`.
 *
 * `-m` is minimum memory. It is taken and ignored: zap has one memory
 * configuration and it is the small one, so there is nothing to shrink from
 * and nothing for a script that passes it to be surprised by.
 *
 * `-i` is now the other one. zap has the warning it names, but off by default
 * and turned on by `-w` -- the reverse of the reference, and the only place
 * the two command lines disagree about what they do rather than how they spell
 * it. The reason is that the check costs 2.1% of a real program and the
 * reference's `-i` does not recover it, because there `-i` silences the
 * printing and leaves the check running. Here the check is the flag. With it
 * off by default, `-i` asks for what is already true, so it is taken and does
 * nothing -- and a command line written for the reference still runs and still
 * gets the bytes it expects.
 *
 * `-w` is not the reference's, which is the one thing this file otherwise
 * never does. It is a flag the reference has no spelling for at all: there is
 * no way to ask ez80asm for the check, because it never turns it off. */
/* `zap prog.s`, with no output named: the output is `prog.bin`.
 *
 * The same walk the sidecar names take, and the same name the reference
 * derives -- an extension replaced where there is one, `.bin` appended where
 * there is not, and any directory kept. Static because the name outlives
 * parse_args and has to last as long as the run does. */
static char derived_output[INCLUDE_NAME_MAX];
static bool sidecar_name(const char* src, const char* ext, char* out, int cap);

__attribute__((noinline)) static bool parse_args(int argc, char* argv[],
                                                 const char** in,
                                                 const char** out,
                                                 bool* stop) {
    *in = NULL;
    *out = NULL;
    *stop = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (a[0] != '-') {
            if (*in == NULL) {
                *in = a;
            } else if (*out == NULL) {
                *out = a;
            }
            continue;
        }
        if (is_ez80_opt(a)) {
            compat_ez80 = true;
            continue;
        }
        const char* const next = (i + 1 < argc) ? argv[i + 1] : NULL;
        int used = 0;
        int v = 0;
        switch (a[1] | 0x20) {
            case 'v':
                printf("zap version %s\r\n", ZAP_VERSION);
                *stop = true;

                return true;
            case 'h':
                usage();
                *stop = true;

                return true;
            case 'o':
                if (!opt_hex(a + 2, next, &used, &v)) {
                    printf("Option -o needs a hexadecimal address\r\n");

                    return false;
                }
                opt_org = v;
                break;
            case 'b':
                if (!opt_hex(a + 2, next, &used, &v)) {
                    printf("Option -b needs a hexadecimal byte\r\n");

                    return false;
                }
                opt_fill = (uint8_t) v;
                break;
            case 'a':
                if (!opt_hex(a + 2, next, &used, &v) || (v != 0 && v != 1)) {
                    printf("Option -a needs 0 or 1\r\n");

                    return false;
                }
                opt_adl = v != 0;
                break;
            case 'c': use_color = false; break;
            case 'l': want_list = true; break;
            case 'd': want_console_list = true; break;
            case 's': want_symbols = true; break;
            case 'x': want_stats = true; break;
            case 'w': want_warn = true; break;
            case 'i': break;   /* the default already ignores them */
            case 'm': break;   /* one memory configuration, and it is small */
            default:
                printf("Unknown option %s\r\n", a);

                return false;
        }
        i += used;
    }
    if (*in == NULL) {
        printf("No input filename\r\n");
        usage();

        return false;
    }
    if (*out == NULL) {
        if (!sidecar_name(*in, ".bin", derived_output,
                          (int) sizeof(derived_output))) {
            printf("Filename too long\r\n");

            return false;
        }
        *out = derived_output;
    }

    return true;
}

/* The failing line, fetched back out of the file.
 *
 * Most failures happen while the line is still in the reader's buffer and are
 * copied from there. An unresolved label is not one of them: it is found when
 * the fixups are patched, long after the loop has moved on, and the fixup
 * carries a line number precisely because the line itself is gone.
 *
 * Keeping the text on every fixup would answer it -- and there are 843 of
 * them in isa_real, in a record that is sixteen bytes so that indexing it is a
 * shift. Reopening the file costs one open, and it costs it **only when the
 * assembly has already failed**, which is the rule the whole of this
 * machinery is built on.
 *
 * Silent if anything goes wrong. A report that cannot be made is not an
 * error; it is one less line of help. */
static void err_reopen(const char* path, int line) {
    if (path == NULL || line <= 0) {
        return;
    }
    buf_reader r;
    if (br_open(&r, path, 1) == NULL) {
        return;
    }
    int n = 0;
    bool too_long = false;
    while (br_fill_lines(&r, &too_long)) {
        const char* p = r.buf_;
        const char* const e = p + r.bsz_;
        while (p < e) {
            const char* q = p;
            while (q < e && *q != '\n') {
                q++;
            }
            if (++n == line) {
                err_line(state.errline, p, q);
                state.errhave = true;
                br_destroy(&r);

                return;
            }
            p = q + 1;
        }
    }
    br_destroy(&r);
}

/* One row of the listing, in the reference's columns.
 *
 *     PC     Output      Line
 *     040000 01 02 03 04 0001   db 1,2,3,4
 *            05 06 07 08
 *
 * Six hex digits of address, then up to four bytes in a twelve-character
 * field, then the line number in four digits, then the source line as it was
 * written. A line that emitted more than four bytes carries on underneath with
 * the address column blank.
 *
 * A macro body line carries its own number and a depth tag -- `0001M1` -- and
 * is written by macro_expand, which also writes the invocation and its
 * arguments; see there.
 *
 * The source text is echoed exactly as written.
 *
 * Whatever cannot be written is dropped: a listing is a convenience and must
 * never be able to fail an assembly. */
/* One line of listing, without its terminator, which is not the same on both
 * destinations.
 *
 * The reference's .lst is LF-terminated -- with one stray CR after the header
 * and nowhere else -- and zap's was CRLF throughout, so two listings of the
 * same source could not be diffed without a filter. The file now matches it
 * byte for byte.
 *
 * The console does not. The reference prints the same LF-only lines to it,
 * which on an Agon means every line of a `-d` listing starts where the last
 * one ended; that is a quirk to leave behind rather than reproduce, and it is
 * on a channel nothing compares. */
/* The listing goes out through a buffer, for the reason the binary does: a
 * write to the card is a MOS call, and this file is produced a line at a time.
 * Unbuffered it cost two calls per line -- the text, then the newline on its
 * own -- which for a 7,500-line program is 21,630 of them, and measured 4.4x
 * on the whole assembly.
 *
 * A kilobyte, which is twenty-odd lines. The point is not to hold the listing;
 * it is to stop paying a MOS call for one byte. */
#define LST_BUF 1024
static char lst_buf[LST_BUF];
static int lst_len;

/* Everything held, in one write.
 *
 * Called before anything seeks in this file and before it is closed.
 * lstfix_apply() goes back over the lines that left a fixup behind, and it has
 * to find them written: a seek past bytes still sitting here would land in the
 * wrong place and the rows it patched would then be overwritten by the flush. */
static void lst_flush(void) {
    if (lst_len > 0) {
        mos_fwrite(list_fh, lst_buf, (uint24_t) lst_len);
        lst_len = 0;
    }
}

static void lst_put(const char* s, int n) {
    if (n > LST_BUF - lst_len) {
        lst_flush();
    }
    if (n > LST_BUF) {
        /* Longer than the buffer, so nothing is gained by copying it in. */
        mos_fwrite(list_fh, (char*) s, (uint24_t) n);

        return;
    }
    memcpy(lst_buf + lst_len, s, (size_t) n);
    lst_len += n;
}

static void list_out(const char* buf, int n) {
    if (want_console_list) {
        printf("%.*s\r\n", n, buf);
    }
    if (list_fh != 0) {
        lst_put(buf, n);
        lst_put("\n", 1);
        state.lst_pos += n + 1;
    }
}

static void list_hex(char* buf, int* w, uint32_t v, int digits) {
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        const int d = (int) ((v >> shift) & 0xF);
        buf[(*w)++] = (char) (d < 10 ? '0' + d : 'A' + d - 10);
    }
}

/* `from` and `to` are positions in the output, not pointers into it. A line
 * that fills the window is flushed while it is still being assembled, and then
 * the bytes it wrote are on the card and not in memory at all -- out_peek
 * fetches them from wherever they ended up. */
void list_line(int pc, int from, int to, int line,
                      int depth, const char* text, const char* tend) {
    /* Static, and that is the whole point of it. 176 bytes of frame put every
     * `buf[w++]` past the signed-byte displacement -- the frame measured 220
     * and the compiler was computing an address for each one. Nothing here
     * re-enters: list_out() writes and returns. */
    static char buf[ERRLINE_MAX + 48];
    int n = to - from;
    int row = 0;

    /* Kept for lstfix_add, which the caller reaches for only when the line
     * left a fixup behind. Free here, where nothing is on the instruction
     * path: this function runs only when a listing is being written. */
    state.lst_lineat = state.lst_pos;
    state.lst_row0 = 0;

    do {
        int w = 0;
        if (row == 0) {
            list_hex(buf, &w, (uint32_t) pc, 6);
            buf[w++] = ' ';
        } else {
            for (int i = 0; i < 7; i++) {
                buf[w++] = ' ';
            }
        }
        int have = n - row * 4;
        if (have > 4) {
            have = 4;
        }
        uint8_t bytes[4];
        if (have > 0 && !out_peek(from + row * 4, bytes, have)) {
            have = 0;
        }
        int put = 0;
        while (put < have) {
            list_hex(buf, &w, bytes[put], 2);
            buf[w++] = ' ';
            put++;
        }
        /* The output field is a fixed twelve characters on every row, so a
         * continuation lines up under the row above it. */
        while (put < 4) {
            buf[w++] = ' ';
            buf[w++] = ' ';
            buf[w++] = ' ';
            put++;
        }
        if (row == 0) {
            /* Four digits, and the depth after them for a macro body. */
            const int d0 = (line / 1000) % 10;
            const int d1 = (line / 100) % 10;
            const int d2 = (line / 10) % 10;
            buf[w++] = (char) ('0' + d0);
            buf[w++] = (char) ('0' + d1);
            buf[w++] = (char) ('0' + d2);
            buf[w++] = (char) ('0' + line % 10);
            if (depth > 0) {
                buf[w++] = 'M';
                buf[w++] = (char) ('0' + (depth % 10));
            }
            buf[w++] = ' ';
            for (const char* q = text; q < tend && *q != '\n'
                                       && w < (int) sizeof(buf) - 3; q++) {
                buf[w++] = *q;
            }
        }
        list_out(buf, w);
        if (row == 0) {
            state.lst_row0 = state.lst_pos - state.lst_lineat;
        }
        row++;
    } while (row * 4 < n);
}

/* Remembers a listed line whose bytes are not final yet.
 *
 * Only the lines that have a fixup in them, which is why this is a list rather
 * than a record per line: a small fraction of the lines in a source hold a
 * forward reference. Nothing here is allocated unless a listing was asked for.
 *
 * A listing that runs out of memory is not a failed assembly: the output bytes
 * are correct either way, and what is lost is that some lines of the listing
 * show what was emitted rather than what was patched. */
void lstfix_add(int from, int to) {
    if (list_fh == 0 || to == from || state.lst_row0 == 0) {
        return;
    }
    if (state.lstfix_used == state.lstfix_cap) {
        Z_SITE("listing fixups");
        const int want = state.lstfix_cap == 0 ? 64 : state.lstfix_cap + state.lstfix_cap;
        lstfix* grown =
            (lstfix*) realloc(state.lstfix, (size_t) want * sizeof(lstfix));
        if (grown == NULL) {
            return;
        }
        state.lstfix = grown;
        state.lstfix_cap = want;
    }
    lstfix* r = &state.lstfix[state.lstfix_used++];
    r->lstat = state.lst_lineat;
    r->row0 = state.lst_row0;
    r->outoff = from;
    r->nbytes = to - from;
}

/* Writes the byte columns of every remembered line again, from the output as
 * it finally stands.
 *
 * Once, at the end, after every fixup has been settled and before the listing
 * file is closed. Each row of a listing is a fixed shape -- six characters of
 * address, a space, then four bytes in a twelve-character field -- so the
 * place a byte was printed is arithmetic: the first row starts where the line
 * does, and every row under it is the same twenty characters long.
 *
 * The console listing cannot be given this. It was printed as the assembly
 * went and is gone; `-d` shows what was emitted. */
static void lstfix_apply(void) {
    for (int i = 0; i < state.lstfix_used; i++) {
        const lstfix* r = &state.lstfix[i];
        for (int row = 0; row * 4 < r->nbytes; row++) {
            /* The first row is as long as its source line made it; the rows
             * under it carry no text at all. */
            const int at = (row == 0)
                               ? r->lstat
                               : r->lstat + r->row0 + (row - 1) * LIST_ROW_LEN;
            /* Read rather than indexed. By the time this runs the window has
             * been flushed and holds none of the output, so these bytes come
             * off the card -- which is why the output file is still open here
             * and closed after. */
            int have = r->nbytes - row * 4;
            if (have > 4) {
                have = 4;
            }
            uint8_t bytes[4];
            if (have > 0 && !out_peek(r->outoff + row * 4, bytes, have)) {
                return;
            }
            char field[12];
            int w = 0;
            int put = 0;
            while (put < have) {
                list_hex(field, &w, bytes[put], 2);
                field[w++] = ' ';
                put++;
            }
            while (put < 4) {
                field[w++] = ' ';
                field[w++] = ' ';
                field[w++] = ' ';
                put++;
            }
            if (mos_flseek(list_fh, (uint32_t) (at + 7)) != 0) {
                return;
            }
            mos_fwrite(list_fh, field, (uint24_t) w);
        }
    }
}

/* The line the reference prints between an invocation and the body it
 * expands to:
 *
 *                            M1 Args: x=7 
 *
 * Twenty-three spaces put the tag under the one the body lines carry, each
 * argument is `name=value` with a space after it, and a macro that takes none
 * says `none`. Values are the argument text as written, which is what was
 * substituted -- not what it evaluates to, which at this point nothing has
 * asked. */
/* The invocation line and the arguments under it, which is everything the
 * reference prints before a body.
 *
 * One function rather than two calls from macro_expand, and out of line for
 * the same reason list_args is: seven arguments and a 176-byte buffer set up
 * inside the expansion is a frame the expansion pays for on every macro in
 * the file, listing or no listing. */
__attribute__((noinline))
void list_invocation(const macro* m, int base, int line, int depth,
                            const char* e) {
    list_line(state.lst_pc, out_written(), out_written(), line, depth, state.lst_p, e);
    list_args(m, base, depth + 1);
}

__attribute__((noinline))
void list_args(const macro* m, int base, int depth) {
    /* Its own frame, and that is the whole reason for the attribute: inlined
     * into macro_expand this buffer took the expansion's frame from 73 bytes
     * to 267, and everything past 128 there is reached through a computed
     * address rather than an `ix` displacement. isa_real read 5.54 against
     * 5.48 for a function that runs only when a listing is being written. */
    char buf[ERRLINE_MAX + 48];
    const int lim = (int) sizeof(buf) - 3;
    int w = 0;
    while (w < 23) {
        buf[w++] = ' ';
    }
    buf[w++] = 'M';
    buf[w++] = (char) ('0' + (depth % 10));
    for (const char* q = " Args: "; *q != 0; q++) {
        buf[w++] = *q;
    }
    if (m->nparam == 0) {
        for (const char* q = "none"; *q != 0 && w < lim; q++) {
            buf[w++] = *q;
        }
    } else {
        const char* pp = m->params;
        for (int k = 0; k < m->nparam; k++) {
            const int pn = (uint8_t) pp[0];
            for (int i = 0; i < pn && w < lim; i++) {
                buf[w++] = pp[1 + i];
            }
            if (w < lim) {
                buf[w++] = '=';
            }
            const char* const a = state.margp[base + k];
            const int an = state.margn[base + k];
            for (int i = 0; i < an && w < lim; i++) {
                buf[w++] = a[i];
            }
            if (w < lim) {
                buf[w++] = ' ';
            }
            pp += pn + 1;
        }
    }
    list_out(buf, w);
}

/* An output file beside the source: `prog.s` gives `prog.symbols`.
 *
 * From the *source* name and not the output, which is what the reference does
 * -- `ez80asm prog.s out.bin -s` writes `prog.symbols`. An extension is
 * replaced if there is one and appended if there is not.
 *
 * Returns false if the name will not fit, which is the only way it can fail.
 */
static bool sidecar_name(const char* src, const char* ext, char* out, int cap) {
    int n = 0;
    int dot = -1;
    while (src[n] != 0) {
        if (src[n] == '.') {
            dot = n;
        } else if (src[n] == '/' || src[n] == '\\' || src[n] == ':') {
            dot = -1;
        }
        n++;
    }
    if (dot >= 0) {
        n = dot;
    }
    int e = 0;
    while (ext[e] != 0) {
        e++;
    }
    if (n + e + 1 > cap) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        out[i] = src[i];
    }
    for (int i = 0; i <= e; i++) {
        out[n + i] = ext[i];
    }

    return true;
}

/* Every global, by name and value, for whatever reads a symbol file next.
 *
 * Globals only. A local belongs to the scope that opened it and is gone by
 * the time this runs -- there is nothing left to export and no name that
 * would mean anything outside. The reference exports the same set.
 *
 * Sorted, because a symbol file that is not is a symbol file nobody can diff.
 * By an array of pointers rather than by moving the nodes, which are pointed
 * at from every fixup that named one.
 *
 * All of it after the assembly is over: not a byte of this is on the path
 * that assembles anything. */
static int sym_cmp(const void* a, const void* b) {
    const sym* const x = *(const sym* const*) a;
    const sym* const y = *(const sym* const*) b;
    const int n = x->len < y->len ? x->len : y->len;
    for (int i = 0; i < n; i++) {
        const uint8_t cx = (uint8_t) x->name[i];
        const uint8_t cy = (uint8_t) y->name[i];
        if (cx != cy) {
            return cx < cy ? -1 : 1;
        }
    }

    return x->len - y->len;
}

static void write_symbols(const char* src) {
    int n = 0;
    for (const symblock* b = state.blocks; b != NULL; b = b->next) {
        n += (b == state.blocks) ? state.syms_used : SYMS_STEP;
    }
    if (n == 0) {
        return;
    }
    const sym** list = (const sym**) malloc((size_t) n * sizeof(sym*));
    if (list == NULL) {
        printf("Cannot export symbols: out of memory\r\n");

        return;
    }
    int k = 0;
    for (const symblock* b = state.blocks; b != NULL; b = b->next) {
        const int used = (b == state.blocks) ? state.syms_used : SYMS_STEP;
        for (int i = 0; i < used; i++) {
            if (b->nodes[i].defined) {
                list[k++] = &b->nodes[i];
            }
        }
    }
    qsort(list, (size_t) k, sizeof(list[0]), sym_cmp);

    char path[INCLUDE_NAME_MAX];
    if (!sidecar_name(src, ".symbols", path, (int) sizeof(path))) {
        free(list);

        return;
    }
    const uint8_t fh = mos_fopen(path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        printf("Cannot write %s\r\n", path);
        free(list);

        return;
    }
    for (int i = 0; i < k; i++) {
        char buf[LABEL_MAX + 24];
        int w = 0;
        for (int j = 0; j < list[i]->len; j++) {
            buf[w++] = list[i]->name[j];
        }
        buf[w++] = ' ';
        buf[w++] = '$';
        /* Upper case and no leading zeros, as the reference writes them. */
        const uint32_t v = (uint32_t) list[i]->addr;
        int shift = 28;
        while (shift > 0 && ((v >> shift) & 0xF) == 0) {
            shift -= 4;
        }
        for (; shift >= 0; shift -= 4) {
            const int d = (int) ((v >> shift) & 0xF);
            buf[w++] = (char) (d < 10 ? '0' + d : 'A' + d - 10);
        }
        buf[w++] = '\r';
        buf[w++] = '\n';
        mos_fwrite(fh, buf, (uint24_t) w);
    }
    mos_fclose(fh);
    free(list);
}

/* What the assembly used, for -x: the counters zap already keeps, rather than
 * anything measured by instrumenting the run. */
static void write_stats(void) {
    int syms = 0;
    for (const symblock* b = state.blocks; b != NULL; b = b->next) {
        syms += (b == state.blocks) ? state.syms_used : SYMS_STEP;
    }
    int names = 0;
    for (const namblock* b = state.names; b != NULL; b = b->next) {
        names += NAMES_BLOCK;
    }
    int macros = 0;
    int macbytes = 0;
    for (const macro* m = state.macros; m != NULL; m = m->next) {
        macros++;
        macbytes += m->bodycap;
    }
    printf("\r\nAssembly statistics\r\n");
    printf("=============================\r\n");
    printf("Label memory         : %6d\r\n", (int) (syms * (int) sizeof(sym) + names));
    printf("Labels               : %6d\r\n", syms);
    printf("\r\nMacro memory         : %6d\r\n", macbytes);
    printf("Macros               : %6d\r\n", macros);
    printf("\r\nOutput               : %6d\r\n", out_here());
    /* The window is a constant, so on its own it says nothing. What is worth
     * knowing is whether the output outgrew it -- and if it did, how much had
     * to be patched behind it, which is what the sweep at the end costs. */
    printf("Output window        : %6d\r\n", state.cap);
    printf("Late patches         : %6d\r\n", state.late_used);
    if (state.fix_used > state.fix_peak) {
        state.fix_peak = state.fix_used;
    }
    printf("\r\nForward references   : %6d\r\n",
           state.fix_used + state.fix_settled);
    printf("Most outstanding     : %6d\r\n", state.fix_peak);
}

/* A value that did not fit where it was written: said, and the assembly
 * carries on.
 *
 * Yellow rather than red, as in the reference, because a file is still going
 * to be produced -- and its bytes are the ones the reference produces, which
 * is why this is a warning in both and an error in neither.
 *
 * The value is printed rather than the source text it was written as. The
 * reference quotes the token; the emitter is several layers below where that
 * text was, and carrying it down would mean holding a pointer and a length for
 * every operand of every line. There is no echoed source line for the same
 * reason. */
/* Where a warning happened, printed the way the reference prints it, and the
 * colour left on for the message that follows. Inside an expansion `state.path`
 * is the macro, which is what the reader needs to be told: the line number
 * counts the body, not the file. */
static void warn_where(void) {
    if (use_color) {
        printf("\033[33m");
    }
    if (state.expanding != 0) {
        printf("Macro [%s] line %d - ", state.path != NULL ? state.path : "?", state.line);
    } else {
        printf("File \"%s\" line %d - ", state.path != NULL ? state.path : "?", state.line);
    }
}

static void warn_done(void) {
    if (use_color) {
        printf("\033[39m");
    }
    printf("\r\n");
}

void warn_trunc(evalue v, int width) {
    warn_where();
    printf("Value truncated to %d bit '0x%lX'", width * 8,
           (unsigned long) (v & 0xFFFFFFFFL));
    warn_done();
}

/* `DS 4, 0xAA` reserves four bytes and does not fill them with 0xAA: a
 * reservation takes the FILLBYTE, and the initializer is dropped. The
 * reference says so and zap said nothing.
 *
 * Not behind `-w`, and it is worth saying why the two differ. `-w` is there
 * because a truncation check is a question asked of every value in every
 * source. This is not a question: it is a fact about a line that has already
 * been parsed and already has an argument nobody will read. It costs a
 * comparison on the DS path, which nothing measures.
 *
 * `-i` does not silence it in the reference either, which is the same
 * distinction drawn there. */
void warn_initializer(const char* t, int n) {
    warn_where();
    printf("Ignoring unsupported initializer value '%.*s'", n, t);
    warn_done();
}

/* What went wrong, said the way somebody trying to fix it needs to hear it.
 *
 * Three things beyond the message. The **source line**, because a line number
 * sends the reader to the file and the line sends them to the mistake. The
 * **token**, where the site that failed had it in hand. And for a macro,
 * **where it was invoked from**, without which a failure inside a body names a
 * line of a file the reader has to guess at.
 *
 * All of it is captured when the failure happens and none of it is maintained
 * in advance, so a source that assembles pays for none of it.
 *
 * The colour codes are the reference's: red for what went wrong, yellow for
 * the text it went wrong in. */
#ifdef KITLOG
/* Appends a line to KITLOG_FILE, for the hardware test kit. See the call site
 * at the end of main for why this exists and why it rewrites the whole file. */
#include <stdarg.h>

#ifndef KITLOG_FILE
#define KITLOG_FILE "kit.log"
#endif

#define KITLOG_MAX 8192

static void kit_log(const char* fmt, ...) {
    static char buf[KITLOG_MAX];
    int n = 0;
    uint8_t fh = mos_fopen(KITLOG_FILE, FA_READ);
    if (fh != 0) {
        n = (int) mos_fread(fh, buf, (uint24_t) (KITLOG_MAX - 256));
        mos_fclose(fh);
        if (n < 0) {
            n = 0;
        }
    }
    va_list ap;
    va_start(ap, fmt);
    n += vsprintf(buf + n, fmt, ap);
    va_end(ap);

    fh = mos_fopen(KITLOG_FILE, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        printf("kit: cannot write %s\r\n", KITLOG_FILE);

        return;
    }
    mos_fwrite(fh, buf, (uint24_t) n);
    mos_fclose(fh);
}
#endif

static void report(const char* in) {
    const char* const red = use_color ? "\033[31m" : "";
    const char* const yellow = use_color ? "\033[33m" : "";
    const char* const off = use_color ? "\033[39m" : "";
    const char* const file =
        state.errfile != NULL ? state.errfile : (state.path != NULL ? state.path : in);

    /* A failure found after the line was read -- an unresolved label -- has
     * no text yet, and the file still has it. */
    if (!state.errhave && state.errmacro == NULL) {
        err_reopen(file, state.line);
    }

    if (state.errmacro != NULL) {
        printf("%sMacro [%s] in \"%s\" line %d - %s", red, state.errmacro, file,
               state.line, zap_err_text[state.err]);
    } else {
        printf("%sFile \"%s\" line %d - %s", red, file, state.line,
               zap_err_text[state.err]);
    }
    if (state.errat != NULL && state.erratlen > 0) {
        printf("%s '%.*s'", yellow, state.erratlen, state.errat);
    }
    printf("%s\r\n", off);

    /* The line as it was written, indent and all, which is how the reader
     * will find it again. */
    if (state.errhave) {
        printf("%s%s%s\r\n", yellow, state.errline, off);
    }
    if (state.errfrompath != NULL) {
        printf("%sInvoked from \"%s\" line %d as%s\r\n", red,
               state.errfrompath, state.errfromline, off);
        if (state.errfrom[0] != 0) {
            printf("%s%s%s\r\n", yellow, state.errfrom, off);
        }
    }
}

/* How main reports failure.
 *
 * On the host a nonzero exit is the convention, and the tests read it. On the
 * Agon nobody reads it: MOS takes main's return as one of its own error codes
 * and prints the message that goes with the number, so a failure reported as
 * 1 comes back on the screen as "Error accessing SD card", and an autoexec or
 * Obey file stops dead at the line that failed. The reference exits zero
 * there for exactly that reason -- EXIT_ERROR is 0 in its non-UNIX build --
 * and everything the operator needs to know has already been printed by
 * report() above. */
#ifdef AGONDEV
#define EXIT_ERROR 0
_Static_assert(EXIT_ERROR == 0, "MOS reads the exit code as one of its own errors");
#else
#define EXIT_ERROR 1
_Static_assert(EXIT_ERROR == 1, "the host reports failure through the exit code");
#endif

int main(int argc, char* argv[]) {
    const char* in;
    const char* out;
    bool stop = false;
    if (!parse_args(argc, argv, &in, &out, &stop)) {
        return EXIT_ERROR;
    }
    if (stop) {
        return 0;
    }

    /* Where the output goes, kept in the state because the window is flushed
     * from deep inside the assembler and has to be able to open it there. */
    state.out_path = out;

    /* After the flag is read: the operator table it builds depends on it. */
    build_tables();
    build_cclass();

    /* The listing is opened before a line is read, so its header sits above
     * the first of them, and closed after the last. Either destination, or
     * both: -l writes the file, -d writes the console, and a run that asks
     * for both gets both. */
    listing = want_list || want_console_list;
    if (want_list) {
        char lpath[INCLUDE_NAME_MAX];
        if (sidecar_name(in, ".lst", lpath, (int) sizeof(lpath))) {
            list_fh = mos_fopen(lpath, FA_WRITE | FA_CREATE_ALWAYS);
            if (list_fh == 0) {
                printf("Cannot write %s\r\n", lpath);
            }
        }
    }

    printf("Assembling %s\r\n", in);
    if (listing) {
        /* The reference writes "\n\r" here and "\n" everywhere after it. The
         * CR is the last byte of the header rather than the first of the
         * next line, which is the same bytes either way. */
        static const char head[] = "PC     Output      Line";
        list_out(head, (int) sizeof(head) - 1);
        if (list_fh != 0) {
            lst_put("\r", 1);
            /* Counted, like everything else written to this file. It is one
             * byte and it is the header's, and leaving it out put every line
             * offset one character to the left -- which lstfix_apply then
             * wrote the bytes into, over the space after the address. */
            state.lst_pos++;
        }
    }
    const clock_t begin = clock();
    const bool ok = run(in);
    const clock_t end = clock();

    if (!ok) {
        report(in);
        /* An assembly that failed leaves no output, even if it had already
         * filled a window and written part of itself out. That is what the
         * reference does -- it writes as it assembles too, and a source that
         * fails at the end leaves nothing behind -- and test/corpus.sh reads a
         * missing file as "this was refused", so a partial one would be read
         * as a disagreement. */
        out_discard();
        dz_free();

        return EXIT_ERROR;
    }

    /* The tail of the output, and then everything the window left behind.
     *
     * In this order because the sweep reads the file: the last window has to
     * be on the card before a chunk covering it can be read back. For an
     * output that never filled the window this opens the file here, writes it
     * once and sweeps in memory, which is what it always did. */
    const int written = out_here();
    /* Created even when there is nothing to put in it. A source that emits no
     * bytes still produces an empty file, as it did when the whole output was
     * written in one call here -- and test/corpus.sh reads the absence of the
     * file as "zap refused this", so not creating it would turn every such
     * source into a disagreement. */
    if (!out_create() || !out_flush() || !resolve_late()) {
        report(in);
        out_discard();
        dz_free();

        return EXIT_ERROR;
    }
    if (list_fh != 0) {
        /* Every fixup is settled by now, so the lines that held one can be
         * given the bytes they actually got. The buffer goes out first:
         * lstfix_apply() seeks, and it can only patch what is on the card.
         *
         * Before the output is closed, not after: the bytes it puts in the
         * listing are read back out of the output file, which by now is where
         * all of them are. */
        lst_flush();
        lstfix_apply();
        mos_fclose(list_fh);
        list_fh = 0;
    }

    if (state.out_fh != 0) {
        mos_fclose(state.out_fh);
        state.out_fh = 0;
    }

    printf("Wrote %s, %d bytes\r\n", out, written);

    /* After the bytes are safe, and only if asked. Neither of these can fail
     * the assembly: the file is written, and a symbol table nobody could save
     * is worth a line of complaint and not an exit code. */
    if (want_symbols) {
        write_symbols(in);
    }
    if (want_stats) {
        write_stats();
    }

#ifdef ZMALLOC
    z_report();
#endif

    const uint24_t cs = elapsed_cs(begin, end);
    printf("Done in %u.%02u seconds\r\n", (unsigned) (cs / 100),
           (unsigned) (cs % 100));

#ifdef KITLOG
    /* A line in a file as well as on the screen, for the hardware test kit.
     *
     * Written by the assembler and not by the script around it, because MOS
     * has no output redirection and a program cannot run another program --
     * so the only thing that can put a timing in a file is the thing that
     * measured it. Off unless the kit asks for it: nothing here is wanted in
     * an ordinary build.
     *
     * Read the whole file and write it back with a line added, rather than
     * opening to append: this MOS refuses FA_OPEN_APPEND and FA_OPEN_ALWAYS,
     * both of which hand back a zero handle. The log is a few hundred bytes. */
    kit_log("zap w=%d src=%s out=%d t=%u.%02u\r\n", state.cap, in, written,
            (unsigned) (cs / 100), (unsigned) (cs % 100));
#endif

    dz_free();

    return 0;
}
