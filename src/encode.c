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

#include "encode.h"

#include "parser.h"

/* Reads the .sis / .lil / .s / .l / .is / .il after a mnemonic and returns the
 * S_* bit it selects. Which bit a one- or two-letter suffix means depends on
 * the current ADL mode, which is why this is not a plain lookup. */
static bool suffix_bit(const char* txt, int sz, bool adl, uint8_t* out) {
    char c0 = txt[0] | 0x20;
    char c1 = sz > 1 ? (txt[1] | 0x20) : 0;
    char c2 = sz > 2 ? (txt[2] | 0x20) : 0;

    switch (sz) {
        case 1:
            if (c0 == 's') { *out = adl ? S_SIL : S_SIS; return true; }
            if (c0 == 'l') { *out = adl ? S_LIL : S_LIS; return true; }

            return false;

        case 2:
            if (c0 != 'i') {
                return false;
            }
            if (c1 == 's') { *out = adl ? S_LIS : S_SIS; return true; }
            if (c1 == 'l') { *out = adl ? S_LIL : S_SIL; return true; }

            return false;

        case 3:
            if (c1 != 'i') {
                return false;
            }
            if (c0 == 's' && c2 == 's') { *out = S_SIS; return true; }
            if (c0 == 's' && c2 == 'l') { *out = S_SIL; return true; }
            if (c0 == 'l' && c2 == 's') { *out = S_LIS; return true; }
            if (c0 == 'l' && c2 == 'l') { *out = S_LIL; return true; }

            return false;

        default:
            return false;
    }
}

static uint8_t suffix_code(uint8_t suffix) {
    if (suffix & S_SIS) return CODE_SIS;
    if (suffix & S_LIS) return CODE_LIS;
    if (suffix & S_SIL) return CODE_SIL;
    if (suffix & S_LIL) return CODE_LIL;

    return 0;
}

/* How many bytes an mmn immediate takes: the suffix decides if there is one,
 * otherwise the ADL mode does. */
static uint8_t imm_size(uint8_t suffix, bool adl) {
    if (suffix != 0) {
        return (suffix & (S_SIS | S_LIS)) ? 2 : 3;
    }

    return adl ? 3 : 2;
}

/* IX and IY encode as HL; the DD or FD prefix is what tells them apart. */
static uint8_t ddfd_prefix(uint32_t reg) {
    if (reg & (R_IX | R_IXH | R_IXL)) {
        return 0xDD;
    }
    if (reg & (R_IY | R_IYH | R_IYL)) {
        return 0xFD;
    }

    return 0;
}

typedef struct _emitted {
    uint8_t suffix;
    uint8_t prefix1;
    uint8_t prefix2;
    uint8_t opcode;
} emitted;

static void transform(emitted* out, operand* op, uint8_t type, parser* p,
                      bool* rel_pending) {
    switch (type) {
        case TR_IR0:
            if (op->reg & (R_IXL | R_IYL)) {
                out->opcode |= 0x01;
            }
            break;
        case TR_IR3:
            if (op->reg & (R_IXL | R_IYL)) {
                out->opcode |= 0x08;
            }
            break;
        case TR_Z:
            out->opcode |= op->reg_index;
            break;
        case TR_Y:
            if (op->has_imm) {
                out->opcode |= (uint8_t) ((op->imm & 0x07) << 3);
            } else {
                out->opcode |= (uint8_t) (op->reg_index << 3);
            }
            break;
        case TR_P:
            out->opcode |= (uint8_t) (op->reg_index << 4);
            break;
        case TR_CC:
            out->opcode |= (uint8_t) (op->cc_index << 3);
            break;
        case TR_N:
            out->opcode |= (uint8_t) op->imm;
            op->has_imm = false;
            break;
        case TR_BIT:
            out->opcode |= (uint8_t) ((op->imm & 0x07) << 3);
            op->has_imm = false;
            break;
        case TR_SELECT: {
            /* Interrupt mode 0, 1, 2 encode as 0, 2, 3. */
            uint8_t y = 0;
            if (op->imm == 1) {
                y = 2;
            } else if (op->imm == 2) {
                y = 3;
            }
            out->opcode |= (uint8_t) (y << 3);
            op->has_imm = false;
            break;
        }
        case TR_REL:
            /* Handled where the byte is written, since it needs the address
             * of the next instruction. */
            *rel_pending = true;
            break;
        default:
            break;
    }
    (void) p;
}

static bool reg_match(uint32_t regset, uint32_t reg) {
    /* Either the row's set includes this register, or neither names one. */
    return (regset & reg) != 0 || (regset | reg) == 0;
}

static const isa_row* match_row(const isa_insn* insn, const operand* a,
                                const operand* b, uint8_t cpu) {
    const uint8_t modeA = a->mode;
    const uint8_t modeB = b->mode;

    for (uint8_t i = 0; i < insn->count; i++) {
        const isa_row* row = &insn->rows[i];

        bool rega = reg_match(row->regsetA, a->reg);
        const bool regb = reg_match(row->regsetB, b->reg);
        bool cond = ((row->condA & MODECHECK) == modeA)
                 && ((row->condB & MODECHECK) == modeB);

        /* A row that takes a condition code accepts one in place of the
         * register it would otherwise want. */
        if (row->flags & F_CCOK) {
            cond = cond || a->cc;
            rega = true;
        }

        if (rega && regb && cond) {
            if ((row->cpu & cpu) == 0) {
                return NULL;  /* the form exists, but not on this CPU */
            }

            return row;
        }
    }

    return NULL;
}

/* Writes an immediate that may still be a forward reference. */
static const char* emit_imm(parser* p, operand* op, uint8_t width) {
    if (!op->imm_known) {
        const fixup_kind kind = (width == 2) ? FIX_ABS16 : FIX_ABS24;

        return pr_stack_fixup(p, op->name, op->name_sz, kind, op->anon);
    }

    for (uint8_t i = 0; i < width; i++) {
        if (!pr_wbyte(p, (uint8_t) ((op->imm >> (i * 8)) & 0xFF))) {
            return pr_msg(p, "output too large");
        }
    }

    return NULL;
}

static const char* emit_row(parser* p, const isa_row* row, operand* a,
                            operand* b, uint8_t suffix) {
    emitted out;
    out.suffix = suffix;
    out.prefix1 = 0;
    out.prefix2 = row->prefix;
    out.opcode = row->opcode;

    if (suffix != 0 && (row->flags & suffix) == 0) {
        return pr_msg(p, "illegal suffix for this instruction");
    }

    /* Range checks the reference makes before emitting anything. */
    if ((row->condA & IMM_BIT) && a->imm_known && (a->imm < 0 || a->imm > 7)) {
        return pr_msg(p, "bit number must be 0..7");
    }
    if ((row->condA & IMM_NSELECT) && a->imm_known && (a->imm < 0 || a->imm > 2)) {
        return pr_msg(p, "interrupt mode must be 0, 1 or 2");
    }
    if (row->transformA == TR_N && a->imm_known && (a->imm & 0x47)) {
        return pr_msg(p, "illegal restart address");
    }
    if ((row->flags & F_DISPA) && (a->disp < -128 || a->disp > 127)) {
        return pr_msg(p, "displacement out of range");
    }
    if ((row->flags & F_DISPB) && (b->disp < -128 || b->disp > 127)) {
        return pr_msg(p, "displacement out of range");
    }

    /* DD/FD says which index register was meant. When both operands name one
     * it is the second that decides, unless the first is the indirect one. */
    if (row->flags & F_DDFDOK) {
        const uint8_t p1 = ddfd_prefix(a->reg);
        const uint8_t p2 = ddfd_prefix(b->reg);
        out.prefix1 = ((p1 == 0 && p2 != 0) || (!a->indirect && p1 != 0 && p2 != 0))
                      ? p2 : p1;
    }

    bool rel_a = false;
    bool rel_b = false;
    transform(&out, a, row->transformA, p, &rel_a);
    transform(&out, b, row->transformB, p, &rel_b);

    /* For DDCBdd the displacement sits between the CB prefix and the opcode
     * rather than after it. */
    const bool dd_before_opcode =
        (out.prefix1 == 0xDD || out.prefix1 == 0xFD) && out.prefix2 == 0xCB
        && (row->flags & (F_DISPA | F_DISPB));

    if (out.suffix != 0 && !pr_wbyte(p, suffix_code(out.suffix))) {
        return pr_msg(p, "output too large");
    }
    if (out.prefix1 != 0 && !pr_wbyte(p, out.prefix1)) {
        return pr_msg(p, "output too large");
    }
    if (out.prefix2 != 0 && !pr_wbyte(p, out.prefix2)) {
        return pr_msg(p, "output too large");
    }
    if (!dd_before_opcode && !pr_wbyte(p, out.opcode)) {
        return pr_msg(p, "output too large");
    }

    if (row->flags & F_DISPA) {
        pr_wbyte(p, (uint8_t) (a->disp & 0xFF));
    }
    if (row->flags & F_DISPB) {
        pr_wbyte(p, (uint8_t) (b->disp & 0xFF));
    }

    /* A relative displacement is measured from the instruction after this
     * one, so it is the last thing written and needs no width decision. */
    if (rel_a || rel_b) {
        operand* rel = rel_a ? a : b;
        if (!rel->imm_known) {
            return pr_stack_fixup(p, rel->name, rel->name_sz, FIX_REL8, rel->anon);
        }
        const int d = (int) rel->imm - (p->addr_ + 1);
        if (d < -128 || d > 127) {
            return pr_msg(p, "relative jump too far");
        }
        pr_wbyte(p, (uint8_t) (d & 0xFF));

        return NULL;
    }

    if (a->has_imm && (row->condA & IMM_N)) {
        pr_wbyte(p, (uint8_t) (a->imm & 0xFF));
    }
    if (b->has_imm && (row->condB & IMM_N)) {
        pr_wbyte(p, (uint8_t) (b->imm & 0xFF));
    }

    if (dd_before_opcode && !pr_wbyte(p, out.opcode)) {
        return pr_msg(p, "output too large");
    }

    const uint8_t width = imm_size(out.suffix, p->adl_);
    if (row->condA & IMM_MMN) {
        const char* err = emit_imm(p, a, width);
        if (err != NULL) {
            return err;
        }
    }
    if (row->condB & IMM_MMN) {
        const char* err = emit_imm(p, b, width);
        if (err != NULL) {
            return err;
        }
    }

    return NULL;
}

/* Finds a mnemonic by name. Only used on the three-operand path, which is
 * rare enough that a scan costs nothing. */
static const isa_insn* find_insn(const char* name) {
    for (int i = 0; i < isa_table_count; i++) {
        const char* a = isa_table[i].name;
        const char* b = name;
        while (*a != 0 && *a == *b) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0) {
            return &isa_table[i];
        }
    }

    return NULL;
}

const char* enc_instruction(parser* p) {
    const int index = (int) p->tk_.tt_;
    if (index < 0 || index >= isa_table_count) {
        return pr_msg(p, "invalid instruction");
    }
    const isa_insn* insn = &isa_table[index];
    const char* err;

    /* An optional suffix, written as a dot straight after the mnemonic. */
    uint8_t suffix = 0;
    next(p);
    if (p->tk_.tk_ == DOT) {
        next(p);
        if (p->tk_.tk_ != NAME && p->tk_.tk_ != REGISTER && p->tk_.tk_ != INSTRUCTION) {
            return pr_msg(p, "invalid suffix");
        }
        if (!suffix_bit(p->tk_.txt_, p->tk_.sz_, p->adl_, &suffix)) {
            return pr_msg(p, "invalid suffix");
        }
        next(p);
    }

    operand a;
    operand b;
    err = op_parse(p, &a);
    if (err != NULL) {
        return err;
    }

    if (p->tk_.tk_ == COMMA) {
        next(p);
        err = op_parse(p, &b);
        if (err != NULL) {
            return err;
        }
    } else {
        op_none(&b);
    }

    /* The undocumented three-operand forms, "res 0,(ix+0),b" and the same for
     * set. They are spelled in the table as res0..res7 and set0..set7, so the
     * bit number moves out of the operands and into the mnemonic, and what
     * was the second operand becomes the first. */
    if (p->tk_.tk_ == COMMA) {
        const char* n = insn->name;
        const bool is_res = n[0] == 'r' && n[1] == 'e' && n[2] == 's' && n[3] == 0;
        const bool is_set = n[0] == 's' && n[1] == 'e' && n[2] == 't' && n[3] == 0;
        if (!is_res && !is_set) {
            return pr_msg(p, "too many operands");
        }
        if (!a.has_imm || !a.imm_known || a.imm < 0 || a.imm > 7) {
            return pr_msg(p, "bit number must be 0..7");
        }

        char name[8];
        name[0] = n[0];
        name[1] = n[1];
        name[2] = n[2];
        name[3] = (char) ('0' + a.imm);
        name[4] = 0;

        insn = find_insn(name);
        if (insn == NULL) {
            return pr_msg(p, "invalid instruction");
        }

        a = b;
        next(p);
        err = op_parse(p, &b);
        if (err != NULL) {
            return err;
        }
    }

    const isa_row* row = match_row(insn, &a, &b, p->cpu_);
    if (row == NULL) {
        return pr_msg(p, "operands do not match this instruction");
    }

    return emit_row(p, row, &a, &b, suffix);
}
