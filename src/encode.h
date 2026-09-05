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

#ifndef _ENCODE_H_
#define _ENCODE_H_

#include "isa.h"
#include "operand.h"

struct _parser;

/* Assembles one instruction. p->tk_ holds the mnemonic, whose tt_ is an index
 * into isa_table; on return p->tk_ is on the newline that ended the line.
 *
 * The mnemonic may carry a suffix -- .sis, .lil, .s, .l, .is, .il -- which
 * selects the operand size for this instruction alone. */
const char* enc_instruction(struct _parser* p);

/* Re-applies a folded operand once its value is known.
 *
 * `aux` is what emit_row recorded: the transform in the low byte and which
 * range check to re-run in the next. Returns the bits to OR into the opcode
 * byte, or an error message if the value is out of range for it. */
const char* enc_fold(int aux, int v, uint8_t* bits);

/* What emit_row records for a folded operand it could not resolve. */
int enc_fold_aux(uint8_t transform, uint16_t cond);

#endif  /* _ENCODE_H_ */
