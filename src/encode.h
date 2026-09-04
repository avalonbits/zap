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

#endif  /* _ENCODE_H_ */
