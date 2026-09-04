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

#ifndef _VALUE_H_
#define _VALUE_H_

#include <stdbool.h>
#include <stdint.h>

/* Numeric literals are held in 32 bits rather than the eZ80's native 24, so
 * that a full 0xFFFFFF stays positive and intermediate results in an
 * expression have room before they are narrowed to the operand width. */
typedef int32_t value;

/* Converts a numeric literal to its value.
 *
 * Recognised forms, in the order they are tried -- the order is load-bearing,
 * because several of them overlap:
 *
 *     $A  $0A       hex, dollar prefix
 *     #A  #0A       hex, hash prefix
 *     %1010         binary, percent prefix
 *     0xA 0XA       hex, C prefix
 *     0b1 0B1       binary, C prefix
 *     Ah  0Ah  0bh  hex, suffix -- claimed before the binary prefix, so
 *                   0bh is 0x0B and 0b1h is 0xB1, not binary
 *     1b  1010b     binary, suffix
 *     255           decimal
 *
 * A single character is always decimal, so the register names a..f are never
 * mistaken for hex digits. Returns false, leaving *out untouched, if the text
 * is not a valid literal in any of these forms.
 *
 * A literal too wide for 24 bits wraps rather than failing, matching the
 * reference assembler, which leaves that for the operand's range check. */
bool num_parse(const char* txt, int sz, value* out);

/* Whether num_parse would accept this text. Used to keep an identifier from
 * shadowing a literal: 'Ah' is a number, so it cannot also be a label. */
bool num_is_literal(const char* txt, int sz);

/* Resolves one backslash escape, given the character that followed the
 * backslash. Returns false for an escape that is not recognised. */
bool esc_char(char ch, char* out);

#endif  /* _VALUE_H_ */
