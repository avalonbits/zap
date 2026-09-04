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

#include "operand.h"

#include "encode.h"
#include "expr.h"
#include "parser.h"

/* The register's bit and its index within the encoding group. The index is
 * what the Y, Z and P transforms shift into the opcode. */
static bool reg_of(TK_TYPE tt, uint32_t* bit, uint8_t* index) {
    switch (tt) {
        case REG_B:  *bit = R_B;   *index = 0; return true;
        case REG_C:  *bit = R_C;   *index = 1; return true;
        case REG_D:  *bit = R_D;   *index = 2; return true;
        case REG_E:  *bit = R_E;   *index = 3; return true;
        case REG_H:  *bit = R_H;   *index = 4; return true;
        case REG_L:  *bit = R_L;   *index = 5; return true;
        case REG_A:  *bit = R_A;   *index = 7; return true;

        case REG_BC: *bit = R_BC;  *index = 0; return true;
        case REG_DE: *bit = R_DE;  *index = 1; return true;
        case REG_HL: *bit = R_HL;  *index = 2; return true;
        case REG_SP: *bit = R_SP;  *index = 3; return true;
        case REG_AF: *bit = R_AF;  *index = 3; return true;

        /* IX and IY encode as HL and are told apart by the DD/FD prefix. */
        case REG_IX: *bit = R_IX;  *index = 2; return true;
        case REG_IY: *bit = R_IY;  *index = 2; return true;

        case REG_IXH: *bit = R_IXH; *index = 4; return true;
        case REG_IXL: *bit = R_IXL; *index = 5; return true;
        case REG_IYH: *bit = R_IYH; *index = 4; return true;
        case REG_IYL: *bit = R_IYL; *index = 5; return true;

        case REG_I:  *bit = R_I;   *index = 0; return true;
        case REG_MB: *bit = R_MB;  *index = 0; return true;
        case REG_RR: *bit = R_R;   *index = 0; return true;

        default:
            return false;
    }
}

/* Returns the condition's index, and whether a relative jump accepts it --
 * only NZ, Z, NC and C can be used with JR. */
static bool cc_of(TK_TYPE tt, uint8_t* index, bool* rel_ok) {
    *rel_ok = false;
    switch (tt) {
        case F_NZ: *index = 0; *rel_ok = true; return true;
        case F_Z:  *index = 1; *rel_ok = true; return true;
        case F_NC: *index = 2; *rel_ok = true; return true;
        /* Carry has no flag token of its own: it would collide with register
         * C, so the lexer hands it back as REG_C and the instruction decides
         * which one it wanted. */
        case F_PO: *index = 4; return true;
        case F_PE: *index = 5; return true;
        case F_P:  *index = 6; return true;
        case F_M:  *index = 7; return true;
        default:
            return false;
    }
}

bool op_empty(const operand* op) {
    return op->reg == R_NONE && !op->cc && !op->has_imm && !op->indirect;
}

void op_none(operand* op) {
    op->reg = R_NONE;
    op->reg_index = 0;
    op->indirect = false;
    op->cc = false;
    op->cc_index = 0;
    op->mode = 0;
    op->has_disp = false;
    op->disp = 0;
    op->has_imm = false;
    op->imm = 0;
    op->imm_known = true;
    op->expr_sz = 0;
    op->expr[0] = 0;
    op->anon = -1;
}

/* Reads an immediate, which may be a symbol that is not defined yet. A bare
 * forward name is kept as a name for the emitter to leave a hole for; anything
 * more involved has to be resolvable now, because there is nowhere to put the
 * rest of the expression. */
/* Reads an immediate.
 *
 * The expression's text is captured as it is read, so that if a name in it
 * turns out not to be defined yet the whole thing can be deferred and
 * re-evaluated later. Only a bare forward name used to be allowed, which meant
 * "ld bc, end - start" -- the ordinary way to write a length -- did not
 * assemble. */
static const char* read_imm(parser* p, operand* op) {
    /* An anonymous forward reference is positional, not textual. */
    if (p->tk_.tk_ == NAME && p->tk_.sz_ == 2 && p->tk_.txt_[0] == '@') {
        value v = 0;
        bool known = false;
        int anon = -1;
        const char* err = pr_resolve(p, p->tk_.txt_, p->tk_.sz_, &v, &known, &anon);
        if (err != NULL) {
            return err;
        }
        if (anon >= 0) {
            op->anon = anon;
            op->imm_known = false;
            op->has_imm = true;
            op->mode |= IMM_MODE;
            /* Resolved by position rather than by text, but the fixup still
             * wants something to hold. */
            op->expr[0] = p->tk_.txt_[0];
            op->expr[1] = p->tk_.txt_[1];
            op->expr[2] = 0;
            op->expr_sz = 2;
            next(p);

            return NULL;
        }
    }

    p->undefined_ = false;
    op->expr_sz = 0;

    /* The text is gathered as the expression is evaluated, so this costs a
     * copy rather than a second parse. */
    value v = 0;
    const char* err = expr_capture(p, &v, op->expr, (int) sizeof(op->expr),
                                   &op->expr_sz);
    if (err != NULL) {
        return err;
    }
    op->imm = v;
    op->has_imm = true;
    op->mode |= IMM_MODE;

    if (p->undefined_) {
        op->imm_known = false;
        op->imm = 0;

        return NULL;
    }
    op->imm_known = true;

    return NULL;
}

const char* op_parse(parser* p, operand* op) {
    op_none(op);

    if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == NONE) {
        return NULL;
    }

    if (p->tk_.tk_ == L_PAREN) {
        op->indirect = true;
        op->mode |= INDIRECT_MODE;
        next(p);

        if (p->tk_.tk_ == REGISTER) {
            uint32_t bit;
            uint8_t index;
            if (!reg_of(p->tk_.tt_, &bit, &index)) {
                return pr_msg(p, "invalid register");
            }
            op->reg = bit;
            op->reg_index = index;
            next(p);

            /* (IX+d) and (IX-d). A minus belongs to the displacement, so it
             * is left in place for the expression to read. */
            if (p->tk_.tk_ == PLUS || p->tk_.tk_ == MINUS) {
                const bool negate = p->tk_.tk_ == MINUS;
                next(p);

                value d = 0;
                const char* err = expr_eval(p, &d);
                if (err != NULL) {
                    return err;
                }
                op->disp = negate ? -(int) d : (int) d;
                op->has_disp = true;

                /* An index register written with a displacement contributes
                 * no register index. The reference only fills reg_index in on
                 * the path without one, and the DDCB forms depend on it: both
                 * transforms there are TR_Z, so "rlc (ix+0),b" would pick up
                 * IX's index of 2 and emit 02 where 00 is meant. */
                op->reg_index = 0;
            }
        } else {
            const char* err = read_imm(p, op);
            if (err != NULL) {
                return err;
            }
        }

        if (p->tk_.tk_ != R_PAREN) {
            return pr_msg(p, "expected )");
        }
        next(p);

        return NULL;
    }

    if (p->tk_.tk_ == REGISTER) {
        uint32_t bit;
        uint8_t index;
        if (!reg_of(p->tk_.tt_, &bit, &index)) {
            return pr_msg(p, "invalid register");
        }
        op->reg = bit;
        op->reg_index = index;

        /* Register C doubles as the carry condition, but it is not written
         * as one: "ld a,c" is a plain register move. Only a row that asks for
         * a condition reads it that way, so no CC bit goes in the mode. */
        if (p->tk_.tt_ == REG_C) {
            op->cc = true;
            op->cc_index = 3;
        }
        next(p);

        /* The shadow accumulator, written "af'". The lexer hands the lone
         * apostrophe back as a bad character literal, which is what it is
         * anywhere else -- "ld a,a'" has to stay an error -- so it is only
         * taken as the shadow marker after AF, the one register that has the
         * spelling. */
        if (op->reg == R_AF && p->tk_.tk_ == BAD_LITERAL
            && p->tk_.sz_ >= 1 && p->tk_.txt_[0] == '\'') {
            /* The bad literal may have swallowed the whitespace after the
             * quote while looking for a closing one -- "ex af,af'" with a
             * trailing tab is written that way in real sources -- so only
             * whitespace is allowed to follow it here. */
            bool only_space = true;
            for (int i = 1; i < p->tk_.sz_; i++) {
                const char c = p->tk_.txt_[i];
                if (c != ' ' && c != '\t' && c != '\r') {
                    only_space = false;
                    break;
                }
            }
            if (only_space) {
                next(p);

                return NULL;
            }
        }

        /* LEA and PEA write their displacement without parentheses --
         * "lea ix, iy+5" -- because they compute an address rather than
         * dereference one. */
        if (p->tk_.tk_ == PLUS || p->tk_.tk_ == MINUS) {
            const bool negate = p->tk_.tk_ == MINUS;
            next(p);

            value d = 0;
            const char* err = expr_eval(p, &d);
            if (err != NULL) {
                return err;
            }
            op->disp = negate ? -(int) d : (int) d;
            op->has_disp = true;
            op->reg_index = 0;
        }

        return NULL;
    }

    if (p->tk_.tk_ == FLAG) {
        uint8_t index;
        bool rel_ok = false;
        if (!cc_of(p->tk_.tt_, &index, &rel_ok)) {
            return pr_msg(p, "invalid condition");
        }
        op->cc = true;
        op->cc_index = index;
        op->mode |= CC_MODE;
        if (rel_ok) {
            op->mode |= CCA_MODE;
        }
        next(p);

        return NULL;
    }

    return read_imm(p, op);
}
