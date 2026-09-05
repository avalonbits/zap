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

#ifndef _MACRO_H_
#define _MACRO_H_

#include <stdbool.h>

#include "value.h"

#define MACRO_MAX_ARGS 8
#define MACRO_MAX      64

/* A macro argument is arbitrary text, not a name: a filename in quotes is
 * passed as one, and those are longer than MAX_NAME. The reference sizes this
 * the same way (MACROARGSUBSTITUTIONLENGTH, its filename limit plus the two
 * quotes). */
#define MACRO_ARG_MAX  66

/* A macro is kept as the text of its body, not as parsed tokens: expansion
 * substitutes arguments into that text and the result is lexed as if it had
 * been written in place. That is what lets "pointless a,b" pass registers --
 * the substitution is textual, so an argument can be anything that is legal
 * where it lands. */
typedef struct _macro {
    char name[MAX_NAME + 1];
    int name_sz;

    char args[MACRO_MAX_ARGS][MAX_NAME + 1];
    int arg_sz[MACRO_MAX_ARGS];
    int argc;

    char* body;
    int body_sz;
} macro;

/* Macros are allocated when they are defined, not reserved.
 *
 * This held macro m[MACRO_MAX] inline: 64 slots of 621 bytes, 39,744 of them,
 * whether the source defined a macro or not -- and macro_table sits inside
 * parser, which is a local in zap_assemble_file, so that was 39 KB of stack on
 * a machine with 512 KB of everything. Most of it was args[8][65], eight
 * argument names at full length for macros that usually take none.
 *
 * An array of pointers is 192 bytes idle, and one allocation per macro
 * actually written. */
typedef struct _macro_table {
    macro* m[MACRO_MAX];
    int count;
} macro_table;

void mt_init(macro_table* mt);
void mt_destroy(macro_table* mt);

/* Adds a macro, taking ownership of body. Returns NULL if the table is full
 * or the name is already taken. */
macro* mt_add(macro_table* mt, const char* name, int name_sz,
              char* body, int body_sz);

/* Finds a macro by name, case-insensitively, as the reserved words are. */
const macro* mt_find(const macro_table* mt, const char* name, int name_sz);

/* Expands a macro body with the given arguments substituted, writing into out.
 * Returns the length, or -1 if it does not fit.
 *
 * An argument name is replaced only where it stands as a whole word, so a
 * macro argument called "one" does not corrupt a label called "oneshot". */
int mt_expand(const macro* m, const char argv[][MACRO_ARG_MAX], const int* argl,
              int argc, char* out, int max);

#endif  /* _MACRO_H_ */
