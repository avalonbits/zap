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

#ifndef _REGISTERS_H_
#define _REGISTERS_H_

#include <stdint.h>

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

#endif  /* _REGISTERS_H_ */
