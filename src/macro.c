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

/* Macro bodies grow a line at a time and are not in the name blocks, because a
 * block is fixed and a body is not known until ENDMACRO. One allocation per
 * macro, doubled when it fills, freed with everything else. */
static bool mark_room(macro* m) {
    if (m->nmarks < m->markcap) {
        return true;
    }
    const int want = m->markcap == 0 ? 8 : m->markcap + m->markcap;
    Z_SITE("macro bodies");
    macmark* grown = (macmark*) realloc(m->marks, (size_t) want * sizeof(macmark));
    if (grown == NULL) {
        return false;
    }
    m->marks = grown;
    m->markcap = want;

    return true;
}

static bool macro_room(macro* m, int need) {
    if (m->bodylen + need <= m->bodycap) {
        return true;
    }
    int want = m->bodycap < 256 ? 256 : m->bodycap + m->bodycap;
    while (m->bodylen + need > want) {
        want += want;
    }
        Z_SITE("macro bodies");
    char* grown = (char*) realloc(m->body, (size_t) want);
    if (grown == NULL) {
        return false;
    }
    m->body = grown;
    m->bodycap = want;

    return true;
}

const macro* macro_at(const char* s, int n) {
    const char c0 = (char) (*s | 0x20);
    macro* prev = NULL;
    for (macro* m = state.macros; m != NULL; prev = m, m = m->next) {
        /* Length and first character before the call, for the same reason the
         * substitution loop asks them: the list is walked once per invocation
         * and most of it is not this macro. */
        if (m->namelen != (uint8_t) n || (m->name[0] | 0x20) != c0) {
            continue;
        }
        if (!same_ci_full(m->name, s, n)) {
            continue;
        }

        /* Found, and moved to the front.
         *
         * Definitions are prepended, so the list is in reverse order of
         * definition -- and a program usually defines its macros in a header
         * and uses them through the rest of the file, which puts the ones it
         * uses most at the far end of the list. Moving a match to the front
         * makes a repeated invocation cost one link.
         *
         * Order carries no meaning here: a second definition of a name is
         * refused where it is written, so there is never more than one
         * match. */
        if (prev != NULL) {
            prev->next = m->next;
            m->next = state.macros;
            state.macros = m;
        }

        return m;
    }

    return NULL;
}

/* `MACRO name [param, param, ...]`.
 *
 * The name and the parameters are copied into the name blocks, which never
 * move; the parameters go in as a length byte followed by the text, so walking
 * them needs no second array and no terminator. */
__attribute__((noinline))
bool macro_begin(const char** pp, const char* e) {
    if (state.defining != NULL) {
        /* "No macro definitions allowed inside a macro" there. */
        state.err = ZAP_E_MACROS_DO_NOT_NEST;

        return false;
    }
    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }
    const char* ns = p;
    while (p < e && name_ch(*p)) {
        p++;
    }
    const int nn = (int) (p - ns);
    if (nn == 0) {
        state.err = ZAP_E_EXPECTED_MACRO_NAME;

        return false;
    }
    /* The same sixty-four characters a label gets, and the same place the
     * reference draws the line: "Macro name too long" at sixty-five. */
    if (nn > LABEL_MAX) {
        err_tok(ns, nn);
        state.err = ZAP_E_MACRO_NAME_TOO_LONG;

        return false;
    }

    if (macro_at(ns, nn) != NULL) {
        /* "Macro already defined" there, and case-blind, as the lookup is. */
        err_tok(ns, nn);
        state.err = ZAP_E_MACRO_ALREADY_DEFINED;

        return false;
    }

    Z_SITE("macro table");
    macro* m = (macro*) calloc(1, sizeof(macro));
    if (m == NULL) {
        state.err = ZAP_E_OUT_MEMORY_MACROS;

        return false;
    }
    /* One byte more than the name, for a terminator. Nothing else in the name
     * blocks carries one -- a label is a pointer and a length -- but an
     * expansion puts this name in `state.path`, where a failure inside the body
     * reports it, and that is read as a string. Without the nul the message
     * came out as the whole arena from the name onwards. */
    char* nm = nam_take(&state.names, &state.names_used, nn + 1);
    if (nm == NULL) {
        free(m);
        state.err = ZAP_E_OUT_MEMORY_MACROS;

        return false;
    }
    for (int i = 0; i < nn; i++) {
        nm[i] = ns[i];
    }
    nm[nn] = 0;
    m->name = nm;
    m->namelen = (uint8_t) nn;

    /* The parameters, each stored as a length and then its characters. */
    for (;;) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (!name_ch(*p)) {
            break;
        }
        const char* ps = p;
        while (p < e && name_ch(*p)) {
            p++;
        }
        const int pn = (int) (p - ps);
        if (pn > 255) {
            /* Freed here, because the macro is not linked into the list until
             * its body has been captured: nothing else can see it yet, so
             * nothing else would ever free it. */
            free(m);
            state.err = ZAP_E_MACRO_PARAMETER_NAME_TOO_LONG;

            return false;
        }
        /* "Invalid argument name" there, for anything the body could not tell
         * from what it stands for: a number in any radix, and any mnemonic or
         * directive. Register names are allowed -- `macro m hl` assembles --
         * because a parameter is substituted by whole identifier and a
         * register is one the body could have meant either way.
         *
         * Asked once per parameter at the definition, which is a dozen times
         * in a file rather than once per expansion. */
        if (numeric_token(ps, pn) || mnemonic_of(ps, pn) != NULL
            || directive_of(ps, pn) != DIR_NONE || is_equ_at(ps)) {
            free(m);
            state.err = ZAP_E_MACRO_PARAMETER_NOT_NUMBER_OR_MNEM;

            return false;
        }
        char* at = nam_take(&state.names, &state.names_used, pn + 1);
        if (at == NULL) {
            free(m);
            state.err = ZAP_E_OUT_MEMORY_MACROS;

            return false;
        }
        if (m->params == NULL) {
            m->params = at;
        }
        at[0] = (char) pn;
        for (int i = 0; i < pn; i++) {
            at[1 + i] = ps[i];
        }
        m->nparam++;
    }

    m->defline = state.line;
    if (state.path != NULL) {
        int pn = 0;
        while (state.path[pn] != 0) {
            pn++;
        }
        char* dp = nam_take(&state.names, &state.names_used, pn + 1);
        if (dp == NULL) {
            free(m);
            state.err = ZAP_E_OUT_MEMORY_MACROS;

            return false;
        }
        for (int i = 0; i < pn; i++) {
            dp[i] = state.path[i];
        }
        dp[pn] = 0;
        m->defpath = dp;
    }

    m->next = state.macros;
    state.macros = m;
    state.defining = m;
    state.line_mode = LINE_CAPTURE;
    *pp = p;

    return true;
}

/* One line of a macro body, copied in as it stands. Nothing on it is parsed
 * until the macro is invoked, which is why a body may hold names that do not
 * exist yet and arguments that are not values. */
/* Every parameter in the span just copied, recorded where it is.
 *
 * This runs once per line of a definition rather than once per identifier per
 * invocation, which is the point of doing it here. Any name character starts a
 * token, digits included: a parameter cannot *be* a number, which is checked
 * where parameters are read, but it can begin with a digit.
 *
 * Matched case-sensitively, which is what the reference does -- `MACRO m v`
 * with `V` in the body is "Unknown identifier" there -- although everything
 * else about a macro is case-blind. */
static bool macro_marks(macro* m, int from, int to) {
    int b = from;
    while (b < to) {
        if (!name_ch(m->body[b])) {
            b++;
            continue;
        }
        int j = b;
        while (j < to && name_ch(m->body[j])) {
            j++;
        }
        const int take = j - b;
        const char* pp = m->params;
        for (int k = 0; k < m->nparam; k++) {
            const int pn = (uint8_t) pp[0];
            /* Length and first character first, so the call is reached only by
             * a candidate that can still match. */
            if (pn == take && pp[1] == m->body[b]
                && same_full(pp + 1, &m->body[b], take)) {
                if (!mark_room(m)) {
                    return false;
                }
                m->marks[m->nmarks].off = b;
                m->marks[m->nmarks].k = (uint8_t) k;
                m->marks[m->nmarks].len = (uint8_t) take;
                m->nmarks++;
                break;
            }
            pp += pn + 1;
        }
        b = j;
    }

    return true;
}

bool macro_line(const char* p, const char* e) {
    const char* q = p;
    while (q < e && *q != '\n') {
        q++;
    }
    const int n = (int) (q - p) + 1;
    if (!macro_room(state.defining, n)) {
        state.err = ZAP_E_OUT_MEMORY_MACROS;

        return false;
    }
    const int at = state.defining->bodylen;
    for (int i = 0; i < n - 1; i++) {
        state.defining->body[at + i] = p[i];
    }
    state.defining->body[at + n - 1] = '\n';
    state.defining->bodylen = at + n;

    /* Only where a parameter could be found: a macro with none has nothing to
     * mark and pays nothing for the feature. */
    if (state.defining->nparam != 0 && !macro_marks(state.defining, at, at + n - 1)) {
        state.err = ZAP_E_OUT_MEMORY_MACROS;

        return false;
    }

    /* Every spelling of a local starts with one: `@name`, `@@`, `@f`, `@b`.
     * So one character decides whether the expansion needs a scope. */
    if (!state.defining->haslocal) {
        for (int i = at; i < at + n - 1; i++) {
            if (state.defining->body[i] == '@') {
                state.defining->haslocal = true;
                break;
            }
        }
    }

    return true;
}

/* The invocation's arguments, as spans of the line that carried them.
 *
 * Held in `zap_state` rather than in a frame: eight pointers and eight lengths is 48
 * bytes, which in the expansion's own frame would push everything else past
 * the 128 bytes an `ix` displacement reaches. Indexed by depth times
 * MACRO_MAXPARAM, which is eight and therefore a shift rather than a call to
 * __imulu.
 *
 * A macro takes as many arguments as it declares, and a mismatch is counted:
 * "0 provided, 1 expected". */
__attribute__((noinline))
static bool macro_args(const macro* m, const char* p, const char* e,
                       const char** stop, int base) {
    int nargs = 0;
    for (;;) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (p >= e || *p == '\n' || *p == ';') {
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (nargs == MACRO_MAXPARAM) {
            state.err = ZAP_E_TOO_MANY_MACRO_ARGUMENTS;

            return false;
        }
        /* The end of the argument is carried forward while scanning, rather
         * than walked back from afterwards.
         *
         * Trimming backwards -- `while (ae > as && is_space_ch(ae[-1])) ae--;`
         * -- is compiled wrongly at -Oz: the decrement is committed before the
         * test, so the byte examined is `ae[-2]`. A single-character argument
         * then sees the space in front of it, trims itself away and expands to
         * nothing. Tracking the last non-space while scanning needs no
         * backward index and is one pass rather than two. */
        const char* as = p;
        const char* ae = p;
        while (p < e && *p != '\n' && *p != ';' && *p != ',') {
            if (!is_space_ch(*p)) {
                ae = p + 1;
            }
            p++;
        }
        state.margp[base + nargs] = as;
        state.margn[base + nargs] = (int) (ae - as);
        nargs++;
    }
    *stop = p;
    if (nargs != m->nparam) {
        state.err = ZAP_E_WRONG_NUMBER_MACRO_ARGUMENTS;

        return false;
    }

    return true;
}

/* One line of the body, with its parameters replaced, ready to assemble.
 *
 * The places a parameter occurs were found when the body was read, so there is
 * nothing to classify here: copy up to the next mark, copy the argument, carry
 * on.
 *
 * `mi` is where the caller has got to in the mark list, which is in body
 * order, so the whole body is walked once across all its lines.
 *
 * Returns the length written, including the newline that ends it, or -1.
 * `bufp` and `capp` are the caller's, so the buffer is grown once and reused
 * for every line of the body and every later invocation at this depth. */
__attribute__((noinline))
static int macro_subst(const macro* m, int lo, int hi, int base,
                       const macmark** mkp, const macmark* mkend,
                       char** bufp, int* capp) {
    char* out = *bufp;
    int cap = *capp;
    int len = 0;
    int cur = lo;
    /* A local cursor, written back once. Through the caller's pointer it
     * would be a load and a store on every mark, which is the fault the
     * comment scan in the line loop carried for a long time. */
    const macmark* mk = *mkp;

    while (mk < mkend && mk->off < hi) {
        const int span = mk->off - cur;
        const int need = state.margn[base + mk->k];
        /* Two spare: the newline this line is finished with, and room for the
         * tail copied after the loop to ask for its own. */
        if (len + span + need + 2 > cap) {
            cap = (len + span + need + 2) * 2;
            Z_SITE("macro expansion");
            char* grown = (char*) realloc(out, (size_t) cap);
            if (grown == NULL) {
                state.err = ZAP_E_OUT_MEMORY_MACROS;

                return -1;
            }
            out = grown;
        }
        const char* b = m->body + cur;
        char* o = out + len;
        for (int i = 0; i < span; i++) {
            o[i] = b[i];
        }
        len += span;
        const char* const arg = state.margp[base + mk->k];
        o = out + len;
        for (int i = 0; i < need; i++) {
            o[i] = arg[i];
        }
        len += need;
        cur = mk->off + mk->len;
        mk++;
    }
    *mkp = mk;

    const int span = hi - cur;
    if (len + span + 2 > cap) {
        cap = (len + span + 2) * 2;
        Z_SITE("macro expansion");
        char* grown = (char*) realloc(out, (size_t) cap);
        if (grown == NULL) {
            state.err = ZAP_E_OUT_MEMORY_MACROS;

            return -1;
        }
        out = grown;
    }
    const char* b = m->body + cur;
    char* o = out + len;
    for (int i = 0; i < span; i++) {
        o[i] = b[i];
    }
    len += span;

    /* The newline the line has to end on: every scan in assemble_line stops
     * on one, and the trailing-text check in macro_expand reads it. */
    out[len++] = '\n';

    *bufp = out;
    *capp = cap;

    return len;
}

/* Assembles the body, a line at a time, in a scope of its own.
 *
 * There is no reader and no nested line loop. The body is already a run of
 * lines -- macro_line stored it that way -- so each one is substituted into a
 * buffer and handed straight to assemble_line. Opening a reader over memory
 * would only find newlines that were never lost, and would cost a reader saved
 * and restored, a nested line loop, and a refill call per invocation.
 *
 * The file the invocation came from stays open: an INCLUDE gives its parent's
 * handle up because MOS has few of them, but an expansion opens no file at
 * all. */
__attribute__((noinline))
bool macro_expand(const macro* m, const char* p, const char* e,
                         const char** stop) {
    if (state.depth >= INCLUDE_MAXDEPTH) {
        state.err = ZAP_E_MACROS_NESTED_TOO_DEEPLY;

        return false;
    }
    const int base = state.depth * MACRO_MAXPARAM;
    if (!macro_args(m, p, e, stop, base)) {
        return false;
    }

    const char* const saved_path = state.path;
    const int saved_line = state.line;
    state.path = m->name;
    state.line = 0;
    state.expanding++;

    char* buf = state.expbuf[state.depth];
    int cap = state.expcap[state.depth];
    const int slot = state.depth;
    state.depth++;

    /* A scope of its own for the body, with the caller's kept and put back,
     * and only for a body that has a local label in it. See `haslocal`. */
    locsave sv;
    if (m->haslocal) {
        scope_push(&sv);
    }

    /* The invocation line, then the arguments it was given. Written here
     * rather than left to the line loop, which writes a line *after*
     * assembling it: that would put the invocation below the body it expanded
     * to, with the bytes of the whole expansion on it. The reference shows the
     * invocation with no bytes and hangs the bytes on the body lines, which is
     * what tells a reader which line of the macro wrote what. */
    if (listing) {
        list_invocation(m, base, saved_line, slot, e);
    }

    bool ok = true;
    /* Offsets into the body, not pointers, because the body may have been
     * realloc'd since the marks were taken and an offset does not care. The
     * mark cursor is a pointer, and the list is in body order, so it walks
     * once across every line. */
    const macmark* mk = m->marks;
    const macmark* const mkend = m->marks + m->nmarks;
    int b = 0;
    const int bend = m->bodylen;
    while (b < bend) {
        int le = b;
        while (le < bend && m->body[le] != '\n') {
            le++;
        }
        /* A body with no parameters is assembled where it lies.
         *
         * Nothing in it can be substituted, so the copy has nothing to do:
         * macro_line already stored the line with the newline that ends it,
         * and assemble_line reads a span rather than a buffer of its own.
         * Half the macros in a real header take no arguments -- a save, a
         * restore, a wait -- and they now cost no buffer, no copy and no
         * call. */
        const char* ls;
        const char* lend;
        if (m->nparam == 0) {
            ls = m->body + b;
            lend = m->body + le + 1;
        } else {
            const int len =
                macro_subst(m, b, le, base, &mk, mkend, &buf, &cap);
            if (len < 0) {
                ok = false;
                break;
            }
            ls = buf;
            lend = buf + len;
        }
        state.line++;

        /* Where this body line starts writing, kept in `zap_state` and not in two
         * locals: live across assemble_line they are two more slots in the
         * body loop, and the loop pays for them on every expansion in the
         * file whether or not anyone asked for a listing. That cost isa_real
         * 5.48 -> 5.52 and bbcbasic 3.84 -> 3.86. The same fields the line
         * loop uses serve here, because a line that is listed by an inner
         * expansion is not listed again by this one. */
        const uint8_t* bo = state.o;
        int bpc = 0;
        if (listing) {
            bpc = state.org + (int) (state.o - state.out);
            state.lst_p = ls;
        }
        /* Two locals: this frame has room for them. */

        const char* st = ls;
        if (!assemble_line(ls, lend, &st)) {
            if (!state.errhave) {
                err_line(state.errline, ls, lend);
                state.errhave = true;
                /* The body line, named as a line of the file it was written
                 * in rather than as an index into the body. */
                state.line = m->defline + state.line;
                state.errfile = m->defpath;
                state.errmacro = m->name;
                /* And the other end of it. This is the only place that holds
                 * both -- by the time the line loop sees the failure, the
                 * path and the line have been given to the macro. */
                state.errfrompath = saved_path;
                state.errfromline = saved_line;
            }
            ok = false;
            break;
        }
        /* Whatever ended the line, exactly as the line loop decides it. The
         * check is written out again here rather than shared: sharing means
         * run_lines calling into it, and that loop runs on every line of every
         * file. suffixed_insn duplicates assemble_line's tail for the same
         * reason. */
        if (*st != '\n') {
            const char* q = st;
            while (q < lend && is_space_ch(*q)) {
                q++;
            }
            if (*q == ';') {
                while (q < lend && *q != '\n') {
                    q++;
                }
            } else if (*q != '\n') {
                state.err = ZAP_E_UNEXPECTED_TEXT_AFTER_INSTRUCTION;
                if (!state.errhave) {
                    err_line(state.errline, ls, lend);
                    state.errhave = true;
                    state.line = m->defline + state.line;
                    state.errfile = m->defpath;
                    state.errmacro = m->name;
                    state.errfrompath = saved_path;
                    state.errfromline = saved_line;
                }
                ok = false;
                break;
            }
        }

        /* The body line, with the bytes it wrote and the depth it wrote them
         * at -- unless it was itself an invocation, which has already written
         * its own listing and its body's. */
        if (listing) {
            if (!state.lst_done) {
                /* The body **as written**, parameters and all, which is what
                 * the reference shows: `db x`, not `db 7`. What x was is on
                 * the Args line above it. */
                list_line(bpc, bo, state.o, state.line, state.depth,
                          m->body + b, m->body + le + 1);
                if (state.fix_touched) {
                    lstfix_add(bo, state.o);
                }
            }
            state.lst_done = false;
            state.fix_touched = false;
        }
        b = le + 1;
    }

    if (listing) {
        /* Everything this invocation had to say is said; the line loop above
         * must not say it again with the whole expansion's bytes on it. */
        state.lst_done = true;
    }

    if (m->haslocal && !scope_pop(&sv)) {
        ok = false;
    }

    state.expbuf[slot] = buf;
    state.expcap[slot] = cap;
    state.depth--;
    state.expanding--;

    if (!ok) {
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
    state.path = saved_path;
    state.line = saved_line;

    return true;
}
