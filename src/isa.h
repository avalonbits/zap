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

#ifndef _ISA_H_
#define _ISA_H_

#include <stdbool.h>
#include <stdint.h>

#include "operand.h"

/* Addressing conditions a row requires of an operand. The first four are
 * matched against the operand; the rest say how wide an immediate is. */
#define NOREQ        0x00
#define INDIRECT     0x01
#define IMM          0x02
#define CC           0x04
#define CCA          0x08
#define IMM_N        0x10
#define IMM_MMN      0x20
#define IMM_BIT      0x40
#define IMM_NSELECT  0x80
#define MODECHECK    (INDIRECT | IMM | CC | CCA)

/* Row flags: displacement required, DD/FD prefix allowed, and which suffixes
 * the instruction accepts. */
#define F_NONE    0x00
#define F_DISPA   0x01
#define F_DISPB   0x02
#define F_CCOK    0x04
#define F_DDFDOK  0x08
#define S_SIS     0x10
#define S_LIS     0x20
#define S_SIL     0x40
#define S_LIL     0x80
#define S_ANY     (S_SIS | S_LIS | S_SIL | S_LIL)
#define S_SISLIL  (S_SIS | S_LIL)
#define S_S1L0    (S_SIL | S_LIS)
#define S_LILLIS  (S_LIL | S_LIS)

/* Which CPU a row belongs to. zap is eZ80-only, so a row is usable when it
 * overlaps CPU_EZ80 -- which brings in the plain Z80 rows and the
 * undocumented ones the eZ80 documents. */
#define BIT_Z80   0x01
#define BIT_U80   0x02
#define BIT_Z180  0x04
#define BIT_Z280  0x08
#define BIT_EZ80  0x10
#define CPU_Z80   (BIT_Z80 | BIT_U80)
#define CPU_Z180  (BIT_Z80 | BIT_Z180)
#define CPU_EZ80  (BIT_Z80 | BIT_EZ80)

/* The suffix bytes themselves. */
#define CODE_SIS  0x40
#define CODE_LIS  0x49
#define CODE_SIL  0x52
#define CODE_LIL  0x5B

/* How an operand feeds the opcode byte. */
typedef enum _isa_transform {
    TR_NONE = 0,
    TR_X,
    TR_Y,
    TR_Z,
    TR_P,
    TR_Q,
    TR_DDFD,
    TR_CC,
    TR_IR0,
    TR_IR3,
    TR_SELECT,
    TR_N,
    TR_BIT,
    TR_REL
} isa_transform;

/* One encoding of one mnemonic: what the two operands have to look like, how
 * they fold into the opcode, and the bytes to emit. */
typedef struct _isa_row {
    uint32_t regsetA;
    uint8_t condA;
    uint32_t regsetB;
    uint8_t condB;
    uint8_t transformA;
    uint8_t transformB;
    uint8_t flags;
    uint8_t cpu;
    uint8_t prefix;
    uint8_t opcode;
} isa_row;

typedef struct _isa_insn {
    const char* name;
    const isa_row* rows;
    uint8_t count;
} isa_insn;

extern const isa_insn isa_table[];
extern const int isa_table_count;

#endif  /* _ISA_H_ */
