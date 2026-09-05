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

#ifndef _OPERAND_H_
#define _OPERAND_H_

#include <stdbool.h>
#include <stdint.h>

#include "lex_types.h"
#include "value.h"

struct _parser;

/* Registers as a bitfield, so a table row can say "any of these" in one word.
 * The layout follows the reference assembler's, which is what the generated
 * instruction table is expressed in. */
#define R_NONE  0x000000UL
#define R_A     0x000001UL
#define R_B     0x000002UL
#define R_C     0x000004UL
#define R_D     0x000008UL
#define R_E     0x000010UL
#define R_H     0x000020UL
#define R_L     0x000040UL
#define R_BC    0x000080UL
#define R_DE    0x000100UL
#define R_HL    0x000200UL
#define R_SP    0x000400UL
#define R_AF    0x000800UL
#define R_IX    0x001000UL
#define R_IY    0x002000UL
#define R_IXH   0x004000UL
#define R_IXL   0x008000UL
#define R_IYH   0x010000UL
#define R_IYL   0x020000UL
#define R_R     0x040000UL
#define R_MB    0x080000UL
#define R_I     0x100000UL

#define RS_NONE 0UL
#define RS_R    (R_A | R_B | R_C | R_D | R_E | R_H | R_L)
#define RS_RR   (R_BC | R_DE | R_HL)
#define RS_IR   (R_IXH | R_IXL | R_IYH | R_IYL)
#define RS_IXY  (R_IX | R_IY)
#define RS_XY   RS_IXY
#define RS_RXY  (R_BC | R_DE | R_IX | R_IY)
#define RS_AE   (R_A | R_B | R_C | R_D | R_E)

/* The addressing bits, matching isa.h's INDIRECT/IMM/CC/CCA. They are spelled
 * out here because isa.h includes this header, not the other way round. */
#define INDIRECT_MODE 0x01
#define IMM_MODE      0x02
#define CC_MODE       0x04
#define CCA_MODE      0x08

/* One parsed operand. Everything an instruction row needs to match against,
 * and everything the emitter needs to produce bytes. */
typedef struct _operand {
    uint32_t reg;      /* which register, as a single bit; R_NONE if not one */
    uint8_t reg_index; /* its index within the encoding group */

    bool indirect;     /* written in parentheses */

    bool cc;           /* usable as a condition code */
    uint8_t cc_index;

    /* The addressing bits a table row is matched against: INDIRECT, IMM, CC,
     * CCA. Register C sets cc without setting CC here -- "ld a,c" is a plain
     * register move, and only a row that asks for a condition (F_CCOK) reads
     * it as carry. */
    uint8_t mode;

    bool has_disp;     /* the d in (IX+d) */
    int disp;

    bool has_imm;

    /* An instruction's immediate is at most three bytes -- a 24-bit address in
     * ADL mode -- so it is held in the machine's word rather than the 32-bit
     * value the evaluator deals in. The emitter writes the low one, two or
     * three bytes and never looks at the rest. */
    int imm;

    /* An immediate that could not be evaluated yet because a name in it is
     * not defined. The emitter leaves a hole and records the expression's
     * text; it is re-evaluated once every symbol is known. The whole
     * expression is kept, not just a name, because "end - start" with both
     * labels further down is how a length is written. */
    bool imm_known;
    char expr[128];
    int expr_sz;
    int anon;
} operand;

/* Parses one operand, starting at the token already in p->tk_ and leaving
 * p->tk_ on whatever ended it -- the comma, the newline, or end of file. */
const char* op_parse(struct _parser* p, operand* op);

/* True when nothing was there to parse. */
bool op_empty(const operand* op);

/* An operand that is not there, for an instruction with only one. */
void op_none(operand* op);

#endif  /* _OPERAND_H_ */
