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

#ifndef EXPR_H
#define EXPR_H

#include "zap.h"
#include "scan.h"

static inline void fwd_reset(const sym* seed) {
    expr_depth = 0;
    expr_fwd = seed;
    expr_fwd2 = NULL;
    expr_fwd_neg = false;
    expr_fwd2_neg = false;
    expr_fwd_bad = false;
}

/* A displacement as the reference keeps it: sixteen bits, signed.
 *
 * Not a detail of the evaluator but of the reference's operand, which holds
 * this field in two bytes -- so `(ix+0x40018)` is 0x18 there and not out of
 * range, while `(ix+0x1008)` is 4104 and is. The signed-byte test the caller
 * makes afterwards is a separate thing and happens on what this returns.
 *
 * Written as arithmetic rather than a cast to int16_t because `int` is three
 * bytes on the eZ80 and four on the host, and this has to be the same number
 * on both. */
static inline int disp_fit(int v) {
    v &= 0xFFFF;

    return v >= 0x8000 ? v - 0x10000 : v;
}

/* Inlined into callers in other files, so the bodies live here. */

static inline bool fwd_result(const sym** target, const sym** sub,
                              bool* subneg) {
    *sub = NULL;
    *subneg = false;
    if (expr_fwd_bad) {
        state.err = ZAP_E_LABEL_DEFINED_ALREADY;

        return false;
    }
    if (expr_fwd2 == NULL) {
        if (expr_fwd_neg) {
            state.err = ZAP_E_LABEL_CANNOT_NEGATED;

            return false;
        }
        *target = expr_fwd;

        return true;
    }
    if (!expr_fwd_neg) {
        *target = expr_fwd;
        *sub = expr_fwd2;
        *subneg = expr_fwd2_neg;
    } else if (!expr_fwd2_neg) {
        *target = expr_fwd2;
        *sub = expr_fwd;
        *subneg = true;
    } else {
        /* Both subtracted. A fixup adds its first symbol, so there is nowhere
         * for `-a - b` to go. */
        state.err = ZAP_E_LABEL_CANNOT_NEGATED;

        return false;
    }

    return true;
}

__attribute__((always_inline)) static inline bool parse_operand(dop* op, const char** pp, const char* e) {
    *op = dop_none;

    const char* p = *pp;
    while (p < e && is_space_ch(*p)) {
        p++;
    }

    /* One class load decides what this operand is: whether the operand list
     * has ended, whether a parenthesis opens it, and whether a register starts
     * here are all one byte's worth of information about one character.
     *
     * The end of the operand list is checked here rather than by bounding the
     * scan at the end of the line, because finding that end would mean another
     * pass over the source. */
    uint8_t cl = cclass[(uint8_t) *p];

    if ((cl & C_OPEND) != 0) {
        *pp = p;

        return true;   /* nothing there */
    }

    if ((cl & C_LPAREN) != 0) {
        op->mode |= INDIRECT;
        p++;
        while (p < e && is_space_ch(*p)) {
            p++;
        }
        cl = cclass[(uint8_t) *p];
    }
    PTRUNC_AT(1);

    /* A register or flag? */
    const char* known_end = NULL;
    if ((cl & C_ALPHA) != 0) {
        const char* s = p;
        while (p < e && name_ch(*p)) {
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
            while (p < e && is_space_ch(*p)) {
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
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
                const char* ds = p;
                /* Bounded, like every character scan here. Without the bound
                 * this loop is compiled rotated: it never examines the first
                 * character and stops one short, so `ld a, 0x42` parses as
                 * 0x4 and leaves `2` behind. Indexing from a base instead of
                 * advancing a pointer does not avoid it. See the header of
                 * this file. */
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
                bool got = false;
                if (digit_ch(*ds)) {
                    const char* q = ds + 1;
                    d = *ds - '0';
                    while (q < p && digit_ch(*q)) {
                        d = d * 10 + (*q - '0');
                        q++;
                    }
                    got = q == p;
                }
                if (!got) {
                    /* Not a plain decimal, so the general parser takes it.
                     *
                     * The fast path must hand over rather than refuse: `05h`
                     * and `0x05` both begin with a digit, so the decimal scan
                     * claims them and then stops on the `h` or the `x`, and
                     * `ld a, (ix + 05h)` is how real sources write it. */
                    value dv = 0;
                    if (num_parse(ds, (int) (p - ds), &dv)) {
                        d = (int) dv;
                        got = true;
                    }
                }
                if (!got) {
                    /* A name or a sum, which is what a structure field looks
                     * like: `RES.LIL 4, (IX+sysvar_vpd_pflags)` is how BBC
                     * BASIC reaches MOS's system variables, and it is most of
                     * that program's use of an index register.
                     *
                     * The sign is applied to the whole of it and not to the
                     * first term -- `(ix-v+1)` with `v` five is -6 in the
                     * reference, not -4 -- which is what negating the result
                     * below already does.
                     *
                     * A name still ahead is carried as a fixup on the
                     * displacement byte, whose position the emitter knows. The
                     * value cannot be checked here -- there is nothing to
                     * check yet -- so the range test moves to patch time with
                     * it, which is where the reference does it too. */
                    p = ds;
                    fwd_reset(NULL);
                    uint8_t dmask = 0;
                    /* Narrowed where it lands, like every value an operand
                     * carries: a displacement is one signed byte and the
                     * range test below is what enforces it. The evaluator is
                     * wider than the machine; nothing an operand holds is.
                     * See evalue. */
                    evalue dv32 = 0;
                    if (!expr_value(&dv32, &p, e, &dmask)) {
                        return false;
                    }
                    d = (int) dv32;
                    if (expr_fwd != NULL) {
                        if (!fwd_result(&op->fwd, &op->fwd2, &op->fwd2_neg)) {
                            return false;
                        }
                        /* The known terms become the addend; the symbol and
                         * the sign are the fixup's. An index operand has no
                         * immediate, so `fwd` is free to mean this -- DISPFWD
                         * is what tells the emitter which it is. */
                        op->mode |= (uint8_t) (neg ? (DISPFWD | DISPNEG) : DISPFWD);
                        op->disp = d;
                        while (p < e && is_space_ch(*p)) {
                            p++;
                        }
                        goto disp_done;
                    }
                }
                op->disp = disp_fit(neg ? -d : d);
                if (op->disp < -128 || op->disp > 127) {
                    /* "Index register offset exceeded" there. One signed byte
                     * is what the instruction has room for, so anything else
                     * would be emitted truncated and silently wrong. */
                    state.err = ZAP_E_INDEX_OFFSET_OUT_RANGE;

                    return false;
                }
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
            disp_done: ;
            }

            if ((op->mode & INDIRECT) != 0) {
                if (*p != ')') {
                    state.err = ZAP_E_EXPECTED;

                    return false;
                }
                p++;
                /* Skips the spaces after the closing parenthesis, which the
                 * general path below already does after its own `)`.
                 *
                 * Needed in one case: finding the comma before a third
                 * operand, as in `res 5, (ix+1) , h`, which the reference
                 * accepts and assembles the same as the unspaced form. Doing
                 * it here rather than in assemble_line confines the cost to
                 * indirect register operands. */
                while (p < e && is_space_ch(*p)) {
                    p++;
                }
            }
            *pp = p;

            return true;
        }

        /* Not a register -- but it can still be a literal.
         *
         * A hexadecimal constant written with a trailing h begins with one of
         * a..f, so `ld hl, aabbcch` arrives here looking exactly like a name.
         * Rewinding to the start of the token is all it takes: num_ch admits
         * letters, so the literal scan below reads the whole thing, and the
         * closing parenthesis of an indirect operand is handled there too.
         *
         * The token is not scanned again to find that out. C_NUM contains
         * every character C_NAME does and three more -- $, # and % -- so the
         * literal scan re-reads exactly the characters just read and stops in
         * the same place, unless the character that ended the name run is one
         * of those three. One class test answers that, where a second pass
         * would walk the whole token, and the token here is usually a label:
         * the longest thing an operand can be.
         *
         * The apostrophe needs no special case, being outside C_NUM as
         * well. */
        if (!num_ch(*nend)) {
            known_end = nend;
        }
        p = s;
    }

    PTRUNC_AT(2);

    /* A literal, or an expression. */
    {
        const char* s = p;
        if (known_end != NULL) {
            /* Already scanned, by the register path that rewound to here. */
            p = known_end;
        } else {
            if (*p == '-' || *p == '+') {
                p++;
            }
            /* Bounded, like every character scan here; see the header of this
             * file. */
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
        int total = 0;
        if (nn == 0) {
            /* Nothing the ordinary scan could take, because the operand begins
             * with a character that is not a C_NUM one: a character literal, a
             * bracketed group, or a unary `~`. The evaluator knows all three,
             * and knows what follows them, so it takes the whole operand.
             *
             * Also where a negated label lands, from the two places below: the
             * fast path reads `-name` as a sign and a name and has nowhere to
             * put the sign, but `-f1 + f2` is representable and the evaluator
             * is what knows that. Reached by goto rather than by testing for
             * it, so the ordinary operand pays nothing for the detour. */
full_expression:
            p = s;
            fwd_reset(NULL);
            uint8_t fwdmask = 0;
            evalue wide = 0;
            if (!expr_value(&wide, &p, e, &fwdmask)) {
                return false;
            }
            total = (int) wide;
            if (!fwd_finish(op)) {
                /* Every way fwd_finish can refuse is a shape the reference
                 * assembles -- a negated label, a complemented one, a third
                 * symbol, any operator but plus and minus -- so all of them
                 * are kept as text rather than refused. */
                if (!defer_expr(s, (int) (p - s), op)) {
                    return false;
                }
                total = 0;
            }
            goto have_value;
        }

        int v = 0;
        bool got = false;
        if (nn == 1 && ns[0] == '$') {
            /* The address of the instruction being assembled. `$` alone; with
             * hex digits after it the scan has already taken them and it is
             * the radix prefix instead. */
            v = state.org + (int) (state.o - state.out);
            got = true;
        } else if (ns[0] == '@') {
            /* `@f` and `@n` are the next anonymous label, `@b` and `@p` the
             * previous one. Reserved spellings, whatever a local of that name
             * would mean -- and a local really can be called `@b`: the
             * reference accepts the definition and then leaves it unreachable,
             * because the reference wins here. Only these exact two-character
             * spellings; `@bb` and `@ff` are ordinary locals. */
            const char k2 = nn == 2 ? (char) (ns[1] | 0x20) : 0;
            if (k2 == 'b' || k2 == 'p') {
                /* Backward is not a reference at all: the address is already
                 * known, so this is the same as a label defined above. */
                if (!state.anon_has_prev) {
                    state.err = ZAP_E_NO_ANONYMOUS_LABEL_ABOVE_ONE;

                    return false;
                }
                v = state.anon_prev;
            } else {
                const sym* sp;
                if (k2 == 'f' || k2 == 'n') {
                    sp = anon_next();
                } else {
#ifdef DUP_LOCINTERN
                    if (loc_intern(ns, nn) == NULL) {
                        return false;
                    }
#endif
                    /* A local label, and nothing else: no radix accepts a
                     * leading at sign, and the reference does not test a local
                     * against the number formats either. Going straight to the
                     * lookup also keeps `@abch` from being read as hexadecimal
                     * by the trailing-h rule below. */
                    sp = loc_intern(ns, nn);
                }
                if (sp == NULL) {
                    return false;
                }
                if (sp->defined) {
                    v = sp->addr;
                } else if (neg) {
                    goto full_expression;
                } else {
                    op->fwd = sp;
                    v = 0;
                }
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
            /* Unsigned for the same reason same_ci is: two signed ints
             * compared with `<` cost a `call pe, __setflag` to repair the
             * flags on overflow, inside a loop that runs once per digit. */
            int acc = ns[0] - '0';
            unsigned k = 1;
            for (; k < (unsigned) nn; k++) {
                if (!digit_ch(ns[k])) {
                    break;
                }
                acc = acc * 10 + (ns[k] - '0');
            }
            if (k == (unsigned) nn) {
                v = acc;
                got = true;
            }
        }
#ifdef DUP_NUMTOK
        if (!got && nn > 0) {
            dup_hash_sink = numeric_token(ns, nn);
        }
#endif
        if (!got && nn > 0 && !numeric_token(ns, nn)) {
            /* Not a literal in any radix, so it is a label.
             *
             * Tried after the literal forms rather than before them, because a
             * hexadecimal constant with a trailing h begins with a letter too:
             * `aabbcch` is a number and `aabbcc` is a name, and only the
             * suffix tells them apart.
             *
             * The decision is whether any radix accepts the token, not what it
             * starts with. `$42`, `#42` and `%1010` are literals that begin
             * with neither a letter nor a digit, while `2b`, `1z` and `123abc`
             * are labels that begin with a digit -- and the reference lets
             * those be referenced as well as defined.
             *
             * A label already defined is its address. One that is not is
             * carried on the operand for the emitter to record, because where
             * the bytes land is not known until the row is chosen. */
#ifdef DUP_INTERN
            if (sym_intern(ns, nn) == NULL) {
                return false;
            }
#endif
            const sym* sp = sym_intern(ns, nn);
            if (sp == NULL) {
                return false;
            }
            if (sp->defined) {
                v = sp->addr;
                got = true;
            } else if (neg) {
                /* `-label` with the address not known yet. On its own it
                 * cannot be represented -- a fixup adds its first symbol --
                 * but `-start + end` can, so the evaluator decides rather
                 * than this path, which has nowhere to keep the sign. */
                goto full_expression;
            } else {
                op->fwd = sp;
                v = 0;
                got = true;
            }
        }
        if (!got) {
            value gv = 0;
            if (nn <= 0 || !num_parse(ns, nn, &gv)) {
                state.err = ZAP_E_EXPECTED_VALUE;

                return false;
            }
            v = (int) gv;
        }
        total = neg ? -v : v;

        /* An expression, if an operator follows the term just read. One table
         * lookup on the character that ended it, which is what the other
         * twenty-eight operands in twenty-nine pay for this feature.
         *
         * A forward reference continues into one by being kept as text: the
         * fixup carries two symbols and a constant, and `TENDIF*256+TTHEN` is
         * not that shape. See defer_expr. */
        {
            const char* q = p;
            while (q < e && is_space_ch(*q)) {
                q++;
            }
            if (exop[(uint8_t) *q] != 0) {
                /* The term already read may itself be a forward reference --
                 * `later + 4` reaches here with `later` in op->fwd -- so the
                 * evaluator is seeded with it rather than refusing. */
                fwd_reset(op->fwd);
                op->fwd = NULL;
                p = q;
                uint8_t fwdmask = fwd_live();
                evalue wide = total;
                if (!expr_climb(&wide, &p, e, 1, 0, &fwdmask)) {
                    return false;
                }
                total = (int) wide;
                if (!fwd_finish(op)) {
                    /* Same as the branch above: an expression a fixup cannot
                     * hold is kept as text rather than refused. This is the
                     * site that matters in practice, because an operand that
                     * begins with a name reaches the expression through here
                     * and not through `full_expression`. */
                    if (!defer_expr(s, (int) (p - s), op)) {
                        return false;
                    }
                    total = 0;
                }
            }
        }

have_value:
        op->imm = total;
        op->mode |= IMM;

        while (p < e && is_space_ch(*p)) {
            p++;
        }
        if ((op->mode & INDIRECT) != 0) {
            if (*p != ')') {
                state.err = ZAP_E_EXPECTED;

                return false;
            }
            p++;

            /* The positional rule, which is what makes `(` mean two things.
             *
             * A parenthesis that opens the operand and whose match closes it
             * is indirection: `ld a, (var)`. One whose match does *not* close
             * it was grouping all along -- `add a, (RTABLE-DTABLE)/2` -- and
             * up to the `/` the two are indistinguishable.
             *
             * So an operand is parsed as indirection first and reinterpreted
             * here, rather than scanned ahead to its matching parenthesis
             * before its shape is known. Scanning ahead would cost every
             * `(hl)` and `(ix+d)` in the file; this costs one table lookup on
             * the operands that reach the general parse with a parenthesis in
             * front, and the register path has already taken the common ones
             * out.
             *
             * The rewind re-reads the operand from the start: `*pp` still
             * holds where it began, because nothing writes it until the
             * end. */
            while (p < e && is_space_ch(*p)) {
                p++;
            }
            if (exop[(uint8_t) *p] != 0) {
                op->mode &= (uint8_t) ~INDIRECT;
                op->fwd = NULL;
                op->fwd2 = NULL;
                s = *pp;

                goto full_expression;
            }
        }
        *pp = p;
    }

    return true;
}

#endif /* EXPR_H */
