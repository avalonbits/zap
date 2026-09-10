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

/* How deep the brackets go, counted in a file-scope byte rather than passed
 * down. A bracket recurses through expr_term and expr_value, both of which run
 * on every term, so a parameter would be paid for by every operand in the file
 * where a byte in memory is paid for by the brackets alone.
 *
 * It has to be counted here and not by `depth` below: `depth` bounds one
 * precedence climb, and a bracket starts a fresh climb at zero, so nesting
 * would otherwise be unbounded. */
static uint8_t expr_depth;

/* The forward references an expression is carrying, and whether they are still
 * in a shape a fixup can hold.
 *
 * A fixup holds two symbols and a constant, and adds the first, so it can
 * represent `k + a`, `k + a - b` and `k + a + b` and nothing else. An
 * expression may therefore name at most two labels that are not yet defined,
 * each joined to the rest by `+` or `-`, with at least one of the two added.
 *
 * A sign is kept per symbol rather than one "still usable" flag for the whole
 * expression, which is what makes `end - start` and `k - later` work.
 *
 * Tracked in file scope rather than returned: returning it would widen every
 * signature in the evaluator, including the recursive ones. Reset per operand
 * in parse_operand, the only place an expression starts.
 *
 * Two named slots rather than an array, because a `const sym*` is three bytes
 * and a variable subscript into an array of them is a call to __imulu. */
const sym* expr_fwd;     /* slot 0, bit 0 of a mask */
const sym* expr_fwd2;    /* slot 1, bit 1 */
bool expr_fwd_neg;

bool expr_fwd2_neg;

bool expr_fwd_bad;

/* Which slots hold a symbol. A term reports the ones it filled by taking this
 * before and after itself, so a bracketed sub-expression needs no special
 * case: whatever it left behind belongs to the term that contained it. */
uint8_t fwd_live(void) {
    uint8_t m = 0;
    if (expr_fwd != NULL) {
        m = 1;
    }
    if (expr_fwd2 != NULL) {
        m |= 2;
    }

    return m;
}

/* Claim a slot for a symbol that is not defined yet. A third one has nowhere
 * to go, and saying so here is the only place that has to know the limit. */
static void fwd_take(const sym* sp) {
    if (expr_fwd == NULL) {
        expr_fwd = sp;
        expr_fwd_neg = false;
    } else if (expr_fwd2 == NULL) {
        expr_fwd2 = sp;
        expr_fwd2_neg = false;
    } else {
        expr_fwd_bad = true;
    }
}

/* Subtraction, and unary minus, flip the sign of everything on their right. */
static void fwd_negate(uint8_t mask) {
    if (mask & 1) {
        expr_fwd_neg = !expr_fwd_neg;
    }
    if (mask & 2) {
        expr_fwd2_neg = !expr_fwd2_neg;
    }
}

__attribute__((noinline))
sym* defer_text(const char* text, int n) {
    if (!sym_room()) {
        state.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }
    if (state.defer_used == state.defer_cap) {
        Z_SITE("deferred expressions");
        const int want = state.defer_cap == 0 ? 8 : state.defer_cap + state.defer_cap;
        defexpr* grown =
            (defexpr*) realloc(state.defer, (size_t) want * sizeof(defexpr));
        if (grown == NULL) {
            state.err = ZAP_E_OUT_MEMORY_LABELS;

            return NULL;
        }
        state.defer = grown;
        state.defer_cap = want;
    }
    char* copy = nam_take(&state.names, &state.names_used, n + 1);
    if (copy == NULL) {
        state.err = ZAP_E_OUT_MEMORY_LABELS;

        return NULL;
    }
    for (int i = 0; i < n; i++) {
        copy[i] = text[i];
    }
    copy[n] = 0;

    sym* sp = &state.blocks->nodes[state.syms_used++];
    sp->next = NULL;
    sp->name = NULL;
    sp->len = 0;
    sp->defined = false;
    sp->islocal = false;
    sp->addr = 0;

    defexpr* d = &state.defer[state.defer_used++];
    d->sp = sp;
    d->text = copy;
    d->len = n;
    d->line = state.line;
    state.err = ZAP_OK;
    fwd_reset(NULL);

    return sp;
}

/* The same for an operand, which carries the symbol as its forward
 * reference and lets the emitter make an ordinary fixup out of it. */
bool defer_expr(const char* text, int n, dop* op) {
    sym* sp = defer_text(text, n);
    if (sp == NULL) {
        return false;
    }
    op->fwd = sp;
    op->fwd2 = NULL;
    op->fwd2_neg = false;
    op->imm = 0;

    return true;
}

bool fwd_finish(dop* op) {
    if (expr_fwd == NULL) {
        return true;
    }

    const sym* target = NULL;
    const sym* sub = NULL;
    bool subneg = false;
    if (!fwd_result(&target, &sub, &subneg)) {
        return false;
    }
    op->fwd = target;
    op->fwd2 = sub;
    op->fwd2_neg = subneg;

    return true;
}

void fwd_reset(const sym* seed) {
    expr_depth = 0;
    expr_fwd = seed;
    expr_fwd2 = NULL;
    expr_fwd_neg = false;
    expr_fwd2_neg = false;
    expr_fwd_bad = false;
}

/* A bare token inside an expression: a number in any radix the reference takes,
 * or a label that is already defined.
 *
 * Already defined is the limit of this stage. A forward reference on its own
 * is still a fixup and still works -- `jp later` is untouched -- but one
 * inside an expression needs the fixup to carry the rest of the sum, which is
 * handled a stage further out rather than guessed at here. */
bool expr_atom(evalue* out, const char* ns, int nn) {
    /* The machine's word: a run this declines falls through to num_parse
     * below, which is where a literal wider than the machine is read. */
    int v = 0;
    if (nn >= 3 && ns[0] == '0' && (ns[1] | 0x20) == 'x') {
        if (hex_digits(ns + 2, nn - 2, &v)) {
            *out = v;

            return true;
        }
    } else if (nn >= 2 && (ns[nn - 1] | 0x20) == 'h') {
        if (hex_digits(ns, nn - 1, &v)) {
            *out = v;

            return true;
        }
    }

    /* Decimal, read here rather than through num_parse.
     *
     * A token that is nothing but digits is a number and can be nothing else,
     * so neither the "could this be a number" check nor the general parser has
     * anything to decide, and both of those are real calls.
     *
     * The accumulation is lit_value's, deliberately: the first digit outside
     * the loop, because `acc * 10` is a call to __imulu here, and the same
     * 24-bit wrap on a value too big to fit, so that `DB 20000000` and
     * `DS 20000000` cannot disagree with each other. */
    if (digit_ch(ns[0]) && nn <= 6) {
        /* Six digits, for the reason at hex_digits: inside the machine's word,
         * so this is narrow arithmetic and anything longer falls to num_parse
         * below rather than being read twice as wide. */
        int acc = ns[0] - '0';
        int k = 1;
        for (; k < nn; k++) {
            if (!digit_ch(ns[k])) {
                break;
            }
            acc = acc * 10 + (ns[k] - '0');
        }
        if (k == nn) {
            *out = acc;

            return true;
        }
    }

    if (!numeric_token(ns, nn)) {
        const sym* sp = NULL;
        if (ns[0] == '@') {
            const char k = nn == 2 ? (char) (ns[1] | 0x20) : 0;
            if (k == 'b' || k == 'p') {
                if (!state.anon_has_prev) {
                    state.err = ZAP_E_NO_ANONYMOUS_LABEL_ABOVE_ONE;

                    return false;
                }
                *out = state.anon_prev;

                return true;
            }
            if (k == 'f' || k == 'n') {
                sp = anon_next();
                if (sp == NULL) {
                    return false;
                }
                fwd_take(sp);
                *out = 0;

                return true;
            }
            sp = loc_intern(ns, nn);
        } else {
            sp = sym_intern(ns, nn);
        }
        if (sp == NULL) {
            return false;
        }
        if (!sp->defined) {
            /* Not known yet. Two of these an expression may have; a third has
             * nowhere to go. */
            fwd_take(sp);
            *out = 0;

            return true;
        }
        *out = sp->addr;

        return true;
    }

    value gv = 0;
    if (!num_parse(ns, nn, &gv)) {
        state.err = ZAP_E_EXPECTED_VALUE;

        return false;
    }
    *out = gv;

    return true;
}

/* One term, with any unary operators in front of it. `fwdmask` comes back
 * holding the slots this term put a forward reference into, so its caller
 * knows which signs its operator has to flip. */
static bool expr_term(evalue* out, const char** pp, const char* e,
                      uint8_t* fwdmask) {
    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }

    /* Unary operators stack the way the reference allows them: it takes one,
     * so `--5` is an error there and here. */
    int sign = 1;
    bool invert = false;
    if (*p == '-' || *p == '+' || *p == '~') {
        if (*p == '-') {
            sign = -1;
        } else if (*p == '~') {
            invert = true;
        }
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p == '-' || *p == '+' || *p == '~' || exop[(uint8_t) *p] != 0) {
            state.err = ZAP_E_UNARY_OPERATOR_VALUE;

            return false;
        }
    }

    const uint8_t fwd_before = fwd_live();
    evalue v = 0;
    if (*p == '[' || *p == '(') {
        /* Both group, and they are the same code because they mean the same
         * thing. A parenthesis reaching here has already been decided not to
         * be indirection -- see the positional rule in parse_operand -- so
         * inside an expression there is nothing left for it to be. */
        const bool square = *p == '[';
        if (expr_depth >= EXPR_MAXDEPTH) {
            state.err = ZAP_E_EXPRESSION_NESTED_TOO_DEEPLY;

            return false;
        }
        expr_depth++;
        p++;
        uint8_t inner = 0;
        if (!expr_value(&v, &p, e, &inner)) {
            return false;
        }
        expr_depth--;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if (*p != (square ? ']' : ')')) {
            state.err = square ? ZAP_E_EXPECTEDX : ZAP_E_EXPECTED;

            return false;
        }
        p++;
    } else if (*p == '\'') {
        /* A character literal is its byte: one character and the closing
         * quote, or a backslash escape and the closing quote.
         *
         * The escapes are the string ones -- one table serves both, which is
         * what the reference does: `'\n'` is 0x0A and so is `"\n"`. */
        if (p[1] == '\\' && !(p[2] == '\'' && p[3] != '\'')) {
            /* An escape, unless the backslash *is* the character. `'\'` is the
             * backslash itself -- the reference's own corpus writes
             * `'[' '\' ']'` for 5B 5C 5D -- and `'\''` is the apostrophe. The
             * two spellings start alike and only the fourth character tells
             * them apart: a closing quote there means the apostrophe was
             * meant, anything else means the literal ended at the quote and
             * the backslash was its content. */
            const int esc = str_escape(p[2]);
            if (esc < 0 || p[3] != '\'') {
                state.err = ZAP_E_EXPECTED_CHARACTER;

                return false;
            }
            v = esc;
            p += 4;
        } else if (p[1] == '\\') {
            v = 0x5C;
            p += 3;
        } else {
            if (p[1] == '\n' || p[1] == 0 || p[2] != '\'') {
                state.err = ZAP_E_EXPECTED_CHARACTER;

                return false;
            }
            v = (uint8_t) p[1];
            p += 3;
        }
    } else {
        const char* const ts = p;
        while (p < e && num_ch(*p)) {
            p++;
        }
        const int n = (int) (p - ts);
        if (n == 0) {
            state.err = ZAP_E_EXPECTED_VALUE;

            return false;
        }
        if (n == 1 && *ts == '$') {
            /* The address of the instruction being assembled. `$` on its own;
             * with hex digits after it, it is the radix prefix instead, and
             * the scan above has already taken them. */
            v = state.org + (int) (state.o - state.out);
        } else if (!expr_atom(&v, ts, n)) {
            return false;
        }
    }

    const uint8_t mine = (uint8_t) (fwd_live() & ~fwd_before);
    *fwdmask = mine;
    if (mine != 0) {
        if (invert) {
            /* `~later`: the fixup adds an address, it cannot complement one.
             * Refused rather than emitted with the operator quietly dropped. */
            expr_fwd_bad = true;
        } else if (sign < 0) {
            fwd_negate(mine);
        }
    }
    if (invert) {
        v = ~v;
    }
    *out = sign < 0 ? -v : v;
    *pp = p;

    return true;
}

/* Operator, right-hand side, fold into the total; again until what follows
 * binds less tightly than this level.
 *
 * A precedence climb, and in `-ez80` mode every operator has the same binding
 * power -- which makes the recursive call below return after a single term and
 * turns the climb into the reference's left-to-right fold. One algorithm, two
 * tables; the compatible answer is not a second code path to keep in step. */
bool expr_climb(evalue* total, const char** pp, const char* e,
                       uint8_t minprec, int depth, uint8_t* fwdmask) {
    const char* p = *pp;
    if (depth > EXPR_MAXDEPTH) {
        state.err = ZAP_E_EXPRESSION_NESTED_TOO_DEEPLY;

        return false;
    }
    for (;;) {
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        const char c = *p;
        if (exop[(uint8_t) c] == 0) {
            break;
        }
        const uint8_t prec = exprec[(uint8_t) c];
        if (prec < minprec) {
            break;
        }
        p++;
        if (c == '<' || c == '>') {
            /* Doubled or nothing: the reference refuses a single one rather
             * than reading it as a comparison, so `1<4` is an error. */
            if (*p != c) {
                state.err = ZAP_E_EXPECTED_OR;

                return false;
            }
            p++;
        }

        /* Left associative, so the right-hand side takes only operators that
         * bind *more* tightly than this one. */
        /* Which slots *this* operator's right-hand side carries, which is not
         * the same as which the expression holds anywhere. In `later + 2*3`
         * the `*` joins two constants and is perfectly fine; asking only "is
         * there a forward reference about" refused it. */
        uint8_t rhs_mask = 0;
        evalue t = 0;
        if (!expr_term(&t, &p, e, &rhs_mask)) {
            return false;
        }
        if (!expr_climb(&t, &p, e, (uint8_t) (prec + 1), depth + 1,
                        &rhs_mask)) {
            return false;
        }
        if ((*fwdmask | rhs_mask) != 0) {
            /* A forward reference survives `+` and `-`, which move it between
             * added and subtracted and nothing more. Every other operator
             * would multiply, mask or shift an address that is not known yet,
             * and a symbol with a sign cannot say that. */
            if (c == '+') {
                *fwdmask |= rhs_mask;
            } else if (c == '-') {
                fwd_negate(rhs_mask);
                *fwdmask |= rhs_mask;
            } else {
                expr_fwd_bad = true;
            }
        }
        switch (c) {
            case '+': *total += t; break;
            case '-': *total -= t; break;
            case '*': *total *= t; break;
            case '/':
                /* The reference divides without looking, which on the Agon is
                 * whatever the runtime does with it. Refusing is the one place
                 * this deliberately does not match: there is no byte sequence
                 * to agree with. */
                if (t == 0) {
                    state.err = ZAP_E_DIVISION_BY_ZERO;

                    return false;
                }
                *total /= t;
                break;
            case '<': *total <<= t; break;
            case '>': *total >>= t; break;
            case '&': *total &= t; break;
            case '|': *total |= t; break;
            default:  *total ^= t; break;
        }
    }
    *pp = p;

    return true;
}

/* A whole expression: a term, then everything that binds to it. */
bool expr_value(evalue* out, const char** pp, const char* e,
                       uint8_t* fwdmask) {
    evalue total = 0;
    if (!expr_term(&total, pp, e, fwdmask)) {
        return false;
    }
    if (!expr_climb(&total, pp, e, 1, 0, fwdmask)) {
        return false;
    }
    *out = total;

    return true;
}

/* ======================================================================
 * EQU
 * ====================================================================== */

/* `EQU`, or `.EQU`, in any case: tested in place, and advancing past it if it
 * is one.
 *
 * Four compares and no token scan. A scan would need somewhere to keep the end
 * of the token, and this runs inside assemble_line where a local costs three
 * bytes of frame.
 *
 * Reading ahead is safe without a bound, and this is not an exception to the
 * rule in the header: there is no loop here to rotate. The chain
 * short-circuits on the first mismatch, and the buffer ends in a newline that
 * matches nothing here, so it can reach the sentinel and never pass it. The
 * class test on the fourth character is what keeps `equx` out.
 *
 * The reference takes `EQU`, `equ` and `.EQU`. It does not take `X EQU 5`
 * without the colon, and it has no `=`. */
bool is_equ_at(const char* p) {
    if (*p == '.') {
        p++;
    }

    return (p[0] | 0x20) == 'e' && (p[1] | 0x20) == 'q' && (p[2] | 0x20) == 'u'
           && (cclass[(uint8_t) p[3]] & C_MNEM) == 0;
}

/* The rest of `name: EQU value`.
 *
 * `named` is the symbol the label path just defined, holding the address the
 * line happened to be at; this replaces it with the value. Defining it first
 * and overwriting is what keeps the ordinary label path unchanged -- and the
 * redefinition check, which has to happen either way, happens there.
 *
 * The value must be knowable now. `X: EQU Y+1` with `Y` still ahead is
 * "Unknown identifier" in the reference too, which has a second pass and could
 * have resolved it: a forward reference in an EQU is refused by both, so this
 * is one of the places where one pass costs nothing.
 *
 * A forward reference *to* an EQU is a different thing and already works.
 * `ld a, X` above `X: EQU 5` goes through the fixup list like any other label
 * that is not defined yet, and patching reads the value the same way it reads
 * an address. Nothing here had to know about it. */
__attribute__((noinline))
bool equ_line(const char* name, int nlen, const char* p,
                     const char* e, const char** stop) {
    /* Steps over the EQU itself. is_equ_at only answered whether one is here,
     * and telling it to also report where it ended meant taking the address of
     * assemble_line's line pointer -- which is the one thing that function
     * cannot afford. Three characters re-read once per EQU against a pointer
     * spilled on every line. */
    if (*p == '.') {
        p++;
    }
    p += 3;
    while (p < e && is_space_ch(*p)) {
        p++;
    }

    /* The label path has already defined this name at the address the line
     * happened to be at, including the check for a second definition and the
     * scope it ends -- a name given a value ends one exactly as a label does,
     * which is what the reference does too. All that is left is to replace the
     * address with the value.
     *
     * Found by name rather than handed over, for the reason given at the call
     * site. Interning a name that exists is a lookup and no allocation. */
    sym* named;
    if (*name == '@') {
        if (nlen == 2 && name[1] == '@') {
            /* An anonymous label has no name to attach a value to, and the
             * reference refuses this too. */
            state.err = ZAP_E_INVALID_LABEL;

            return false;
        }
        named = loc_intern(name, nlen);
    } else {
        named = sym_intern(name, nlen);
    }
    if (named == NULL) {
        return false;
    }

    /* Undefined while its own value is being worked out, so that `X: EQU X+1`
     * is refused rather than quietly reading the address the label path just
     * gave it. The reference refuses it too. Two stores, on the EQU path
     * only. */
    named->defined = false;

    evalue value = 0;
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
    named->defined = true;
    named->addr = value;

    while (p < e && is_space_ch(*p)) {
        p++;
    }
    *stop = p;

    /* No test for what follows: the line loop reports anything left over, so
     * `X: EQU 5 6` is "unexpected text after the instruction" from there. */

    return true;
}

/* Another buffer of whole lines, or not.
 *
 * Out of line, and the `bool` it needs is the reason. The reader reports an
 * over-long line through an out-parameter, and holding that across the call
 * inside `run` took its frame from 13 bytes to 16 -- in the function that
 * holds the line loop. Here it costs nothing, because this is entered once per
 * 16 KB of source. A failure is told from an end of file by `state.err`, which
 * exists either way.
 *
 * The check itself is new. The reader has always said when one line does not
 * fit its buffer and the loop has always ignored it, ending the assembly
 * *successfully* and writing whatever came before -- unreachable until a
 * directive could put 16 KB on one line, and wrong the whole time regardless. */
__attribute__((noinline))
bool line_fill(buf_reader* r) {
    bool too_long = false;
    if (br_fill_lines(r, &too_long)) {
        return true;
    }
    if (too_long) {
        /* state.line counts the lines already assembled, so this names the one
         * before the offending line. That is still where to start looking. */
        state.line++;
        state.err = ZAP_E_LINE_TOO_LONG;
    }

    return false;
}

__attribute__((noinline))
bool scope_push(locsave* sv) {
    sv->loccur = state.loccur;
    sv->locnames = state.locnames;
    sv->locs_used = state.locs_used;
    sv->locnames_used = state.locnames_used;
    sv->lfix_used = state.lfix_used;
    sv->subfix_used = state.subfix_used;
    sv->undo_used = state.undo_used;
    sv->gen = state.gen;
    /* The scope end a global label left pending belongs to the caller. Left
     * standing, the first local in the body would notice it -- the body counts
     * its own line numbers, so the comparison that defers it never matches --
     * and end the expansion's scope in the caller's name, clearing the flag.
     * The caller's next local then would not open a scope at all, and the one
     * above it would still hold whatever the previous scope defined. */
    sv->scope_line = state.scope_line;
    state.scope_line = 0;

    if (++state.gen == 0) {
        /* The stamp has wrapped, so a bucket left over from 256 scopes ago
         * would read as belonging to this one. The undo log is what puts them
         * back, and it records what it finds, so emptying them here is safe. */
        for (int b = 0; b < NLOCB; b++) {
            state.locs[b].gen = 0;
            state.locs[b].head = NULL;
        }
        state.gen = 1;
    }

    return true;
}

/* Settles what the expansion referred to and puts the caller's scope back.
 *
 * Only the fixups the expansion added are resolved -- the caller's are still
 * outstanding and belong to a scope that has not ended. */
__attribute__((noinline))
bool scope_pop(locsave* sv) {
    /* The body's locals go the same way a scope's do, so a fixup that names a
     * global and one of them is settled in halves here too -- and only the
     * ones this expansion added: the caller's are still outstanding and its
     * locals are not defined yet. */
    bool ok = fold_subs(sv->subfix_used);
    for (int i = sv->lfix_used; ok && i < state.lfix_used; i++) {
        if (!patch_fixup(&state.lfixups[i])) {
            ok = false;
            break;
        }
    }

    while (state.undo_used > sv->undo_used) {
        const locundo* u = &state.undo[--state.undo_used];
        state.locs[u->b].head = u->head;
        state.locs[u->b].gen = u->gen;
    }
    state.gen = sv->gen;
    state.scope_line = sv->scope_line;
    state.lfix_used = sv->lfix_used;

    /* The arena counters go back so the space is handed out again. The name
     * blocks only rewind if the body did not need a new one: nam_take pushes a
     * new block in front of the list, and rewinding past it would lose it. */
    state.loccur = sv->loccur;
    state.locs_used = sv->locs_used;
    if (state.locnames == sv->locnames) {
        state.locnames_used = sv->locnames_used;
    }

    return ok;
}
