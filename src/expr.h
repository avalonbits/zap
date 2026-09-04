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

#ifndef _EXPR_H_
#define _EXPR_H_

#include "value.h"

struct _parser;

/* Evaluates an expression.
 *
 * The first token of the expression must already be in p->tk_. On return,
 * p->tk_ holds the first token that is not part of the expression -- the comma,
 * newline or paren that ended it -- which is the same convention the rest of
 * the parser follows.
 *
 * Operators are applied strictly left to right, with no precedence at all:
 * 1+2*3 is 9, not 7, and 1+2<<2 is 12, not 9. That is not an oversight. The
 * reference assembler evaluates this way, its own corpus depends on it, and
 * zap has to produce the same bytes. Brackets are the only way to group;
 * parentheses cannot be, because they already mean indirect addressing.
 *
 * Supported: the binary operators + - * / << >> & | ^, the unary operators
 * + - ~, bracketed subexpressions, numeric and character literals, '$' for the
 * current program counter, and identifiers resolved against the label table.
 *
 * Returns NULL on success, or an error message ready to hand back to the
 * caller. */
const char* expr_eval(struct _parser* p, value* out);

/* Evaluates an expression and copies the text of exactly the tokens it
 * consumed into `text`, separated by single spaces so it re-lexes the same
 * way. An operand uses this so that an expression naming a symbol that is not
 * defined yet can be stored and evaluated again once it is.
 *
 * A name that is not defined is not an error here: p->undefined_ is set and
 * evaluation carries on with zero, leaving the caller to decide whether it
 * can defer. */
const char* expr_capture(struct _parser* p, value* out, char* text,
                         int max, int* text_sz);

#endif  /* _EXPR_H_ */
