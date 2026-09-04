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

#include "expr.h"

#include <stdbool.h>

#include "hash_table.h"
#include "parser.h"

/* Nesting is bounded so a pathological line cannot run the stack out on a
 * machine with 32KB of it. Real sources nest one or two deep. */
#define MAX_DEPTH 16

static const char* eval_at(parser* p, value* out, int depth);

static bool is_binary_op(TOKEN tk) {
    switch (tk) {
        case PLUS:
        case MINUS:
        case STAR:
        case F_SLASH:
        case AMPERSAND:
        case PIPE:
        case CARET:
        case SHIFT_L:
        case SHIFT_R:
            return true;
        default:
            return false;
    }
}

/* Shifts are done on the unsigned bit pattern: a shift of a negative value, or
 * one that pushes bits off the top, is implementation-defined for signed types
 * and the reference simply shifts the bits. */
static value apply(TOKEN op, value a, value b, bool* div_zero) {
    switch (op) {
        case PLUS:      return a + b;
        case MINUS:     return a - b;
        case STAR:      return a * b;
        case F_SLASH:
            if (b == 0) {
                *div_zero = true;

                return 0;
            }

            return a / b;
        case AMPERSAND: return a & b;
        case PIPE:      return a | b;
        case CARET:     return a ^ b;
        case SHIFT_L:   return (value) ((uint32_t) a << (b & 31));
        case SHIFT_R:   return (value) ((uint32_t) a >> (b & 31));
        default:        return a;
    }
}

/* One operand, with any run of unary operators in front of it. */
static const char* operand_at(parser* p, value* out, int depth) {
    /* Unary operators are collected first and applied on the way out, so
     * 1+~1 negates only the 1 and -~0 works. */
    TOKEN unary[MAX_DEPTH];
    int nunary = 0;
    while (p->tk_.tk_ == PLUS || p->tk_.tk_ == MINUS || p->tk_.tk_ == TILDE) {
        if (nunary == MAX_DEPTH) {
            return pr_msg(p, "expression too deep");
        }
        unary[nunary++] = p->tk_.tk_;
        next(p);
    }

    value v = 0;
    switch (p->tk_.tk_) {
        case NUMBER:
            v = p->tk_.val_;
            next(p);
            break;

        case DOLLAR:
            /* A bare '$' is where this instruction starts. */
            v = (value) (p->pos_ + p->org_);
            next(p);
            break;

        case L_BRACKET: {
            next(p);
            const char* err = eval_at(p, &v, depth + 1);
            if (err != NULL) {
                return err;
            }
            if (p->tk_.tk_ != R_BRACKET) {
                return pr_msg(p, "expected ]");
            }
            next(p);
            break;
        }

        case NAME: {
            bool ok = false;
            const int addr = ht_nget(&p->labels_, p->tk_.txt_, p->tk_.sz_, &ok);
            if (!ok) {
                return pr_msg(p, "undefined symbol");
            }
            v = (value) addr;
            next(p);
            break;
        }

        case BAD_LITERAL:
            return pr_msg(p, "invalid character literal");

        default:
            return pr_msg(p, "expected a value");
    }

    while (nunary > 0) {
        const TOKEN op = unary[--nunary];
        if (op == MINUS) {
            v = -v;
        } else if (op == TILDE) {
            v = ~v;
        }
    }
    *out = v;

    return NULL;
}

static const char* eval_at(parser* p, value* out, int depth) {
    if (depth >= MAX_DEPTH) {
        return pr_msg(p, "expression too deep");
    }

    value acc = 0;
    const char* err = operand_at(p, &acc, depth);
    if (err != NULL) {
        return err;
    }

    /* Strictly left to right -- see the note in expr.h. Every operator folds
     * into the accumulator the moment it is read, which is what makes
     * 1+2*3 nine. */
    while (is_binary_op(p->tk_.tk_)) {
        const TOKEN op = p->tk_.tk_;
        next(p);

        value rhs = 0;
        err = operand_at(p, &rhs, depth);
        if (err != NULL) {
            return err;
        }

        bool div_zero = false;
        acc = apply(op, acc, rhs, &div_zero);
        if (div_zero) {
            return pr_msg(p, "division by zero");
        }
    }
    *out = acc;

    return NULL;
}

const char* expr_eval(parser* p, value* out) {
    return eval_at(p, out, 0);
}
