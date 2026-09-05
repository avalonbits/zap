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
 * machine with 32KB of it. Real sources nest one or two deep; the limit is
 * well past anything the reference's own corpus reaches, and past what it
 * accepts before its own recursion gives out. */
#define MAX_DEPTH 32

/* Where the text of the consumed tokens is being gathered, if it is. */
typedef struct _capture {
    char* buf;
    int max;
    int n;
    bool overflow;
} capture;

static const char* eval_at(parser* p, value* out, int depth, capture* cap);

/* Records the token about to be consumed. Tokens are separated by a space so
 * the text re-lexes into the same sequence. */
static void cap_token(capture* cap, const token* tk) {
    if (cap == NULL || cap->overflow) {
        return;
    }
    if (cap->n > 0) {
        if (cap->n >= cap->max) {
            cap->overflow = true;

            return;
        }
        cap->buf[cap->n++] = ' ';
    }
    for (int i = 0; i < tk->sz_; i++) {
        if (cap->n >= cap->max) {
            cap->overflow = true;

            return;
        }
        cap->buf[cap->n++] = tk->txt_[i];
    }
}

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

/* The right shift is arithmetic -- it keeps the sign -- because the reference
 * shifts a signed int32 and its results depend on that: -87>>10 is -1 there,
 * not 4194302. The left shift goes through unsigned so that pushing bits off
 * the top stays defined; the low bits, which are all that survives into an
 * operand, come out the same either way.
 *
 * The count is masked to 31 to match what the reference gets from the
 * hardware for an over-wide shift, rather than leaving it undefined. */
static value apply(TOKEN op, value a, value b, bool* div_zero) {
    /* Add, subtract and multiply go through unsigned so that an intermediate
     * result too wide for the operand wraps instead of being undefined.
     * db 0x7fffffff+1 used to trip UBSan; the bits that reach the operand are
     * the same either way. */
    const uint32_t ua = (uint32_t) a;
    const uint32_t ub = (uint32_t) b;

    switch (op) {
        case PLUS:      return (value) (ua + ub);
        case MINUS:     return (value) (ua - ub);
        case STAR:      return (value) (ua * ub);
        case F_SLASH:
            if (b == 0) {
                *div_zero = true;

                return 0;
            }
            /* The one signed division that overflows. On x86 it traps, so the
             * reference dies here the same way it dies on a division by zero;
             * zap wraps to the value the operation would have produced. */
            if (a == INT32_MIN && b == -1) {
                return a;
            }

            return a / b;
        case AMPERSAND: return a & b;
        case PIPE:      return a | b;
        case CARET:     return a ^ b;
        case SHIFT_L:   return (value) ((uint32_t) a << (b & 31));
        case SHIFT_R:   return a >> (b & 31);
        default:        return a;
    }
}

/* One operand, with any run of unary operators in front of it. */
static const char* operand_at(parser* p, value* out, int depth, capture* cap) {
    /* At most one unary operator. A run of them -- --1, -~1, ~~1 -- is
     * rejected, which is what the reference does ("Illegal unary operator").
     * 1--1 is still fine: that is a binary minus followed by a unary one. */
    TOKEN unary = NONE;
    if (p->tk_.tk_ == PLUS || p->tk_.tk_ == MINUS || p->tk_.tk_ == TILDE) {
        unary = p->tk_.tk_;
        cap_token(cap, &p->tk_);
        next(p);

        if (p->tk_.tk_ == PLUS || p->tk_.tk_ == MINUS || p->tk_.tk_ == TILDE) {
            return pr_msg(p, "illegal unary operator");
        }
    }

    value v = 0;
    switch (p->tk_.tk_) {
        case NUMBER:
            v = p->tk_.val_;
            cap_token(cap, &p->tk_);
            next(p);
            break;

        case DOLLAR:
            /* A bare '$' is where this statement starts, not where the operand
             * being read sits: "jp $" jumps to the jp itself, and by now its
             * opcode has already been emitted. */
            v = (value) p->stmt_addr_;
            p->pc_used_ = true;
            cap_token(cap, &p->tk_);
            next(p);
            break;

        case L_BRACKET: {
            cap_token(cap, &p->tk_);
            next(p);
            const char* err = eval_at(p, &v, depth + 1, cap);
            if (err != NULL) {
                return err;
            }
            if (p->tk_.tk_ != R_BRACKET) {
                return pr_msg(p, "expected ]");
            }
            cap_token(cap, &p->tk_);
            next(p);
            break;
        }

        case NAME:
        case INSTRUCTION:
        case DIRECTIVE: {
            bool known = false;
            int anon = -1;
            const char* err = pr_resolve(p, p->tk_.txt_, p->tk_.sz_, &v,
                                         &known, &anon);
            if (err != NULL) {
                return err;
            }
            if (!known) {
                /* Not an error by itself: an operand defers the whole
                 * expression and re-evaluates it once every symbol is known.
                 * Somewhere that cannot defer -- a directive that has to size
                 * something now -- turns this into the error. */
                p->undefined_ = true;
                pr_note_waiting(p, p->tk_.txt_, p->tk_.sz_);
                v = 0;
            }
            cap_token(cap, &p->tk_);
            next(p);
            break;
        }

        case BAD_LITERAL:
            return pr_msg(p, "invalid character literal");

        default:
            return pr_msg(p, "expected a value");
    }

    if (unary == MINUS) {
        v = -v;
    } else if (unary == TILDE) {
        v = ~v;
    }
    *out = v;

    return NULL;
}

static const char* eval_at(parser* p, value* out, int depth, capture* cap) {
    if (depth >= MAX_DEPTH) {
        return pr_msg(p, "expression too deep");
    }

    value acc = 0;
    const char* err = operand_at(p, &acc, depth, cap);
    if (err != NULL) {
        return err;
    }

    /* Strictly left to right -- see the note in expr.h. Every operator folds
     * into the accumulator the moment it is read, which is what makes
     * 1+2*3 nine. */
    while (is_binary_op(p->tk_.tk_)) {
        const TOKEN op = p->tk_.tk_;
        cap_token(cap, &p->tk_);
        next(p);

        value rhs = 0;
        err = operand_at(p, &rhs, depth, cap);
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
    /* Cleared on entry: the flag is set by whichever name was undefined in
     * *this* expression. Leaving it sticky made a deferred operand poison the
     * next directive that evaluated anything. */
    p->undefined_ = false;
    p->pc_used_ = false;
    p->wait_hash_ = 0;

    const char* err = eval_at(p, out, 0, NULL);
    if (err == NULL && p->undefined_) {
        /* Nowhere to defer to here. */
        return pr_msg(p, "undefined symbol");
    }

    return err;
}

const char* expr_capture(parser* p, value* out, char* text, int max,
                         int* text_sz) {
    /* wait_hash_ too: it names the symbol *this* expression is waiting for,
     * so leaving it set from the last one files every later fixup under a
     * name that has already been defined, and none of them are ever settled. */
    p->undefined_ = false;
    p->pc_used_ = false;
    p->wait_hash_ = 0;

    capture cap;
    cap.buf = text;
    cap.max = max - 1;
    cap.n = 0;
    cap.overflow = false;

    const char* err = eval_at(p, out, 0, &cap);
    if (err != NULL) {
        return err;
    }
    if (cap.overflow) {
        return pr_msg(p, "expression too long");
    }
    text[cap.n] = 0;
    *text_sz = cap.n;

    return NULL;
}
