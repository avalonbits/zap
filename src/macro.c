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

#include "macro.h"

#include <stdlib.h>

static char lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char) (ch + 0x20);
    }

    return ch;
}

static bool same_name(const char* a, int an, const char* b, int bn) {
    if (an != bn) {
        return false;
    }
    for (int i = 0; i < an; i++) {
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }

    return true;
}

/* The characters that can be part of a name, matching the lexer's idea of
 * one. A substitution only fires between two of these. */
static bool name_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
        || (ch >= '0' && ch <= '9') || ch == '_' || ch == '@';
}

void mt_init(macro_table* mt) {
    mt->count = 0;
}

void mt_destroy(macro_table* mt) {
    for (int i = 0; i < mt->count; i++) {
        free(mt->m[i].body);
        mt->m[i].body = NULL;
    }
    mt->count = 0;
}

macro* mt_add(macro_table* mt, const char* name, int name_sz,
              char* body, int body_sz) {
    if (mt->count == MACRO_MAX || name_sz <= 0 || name_sz > MAX_NAME) {
        return NULL;
    }
    if (mt_find(mt, name, name_sz) != NULL) {
        return NULL;
    }

    macro* m = &mt->m[mt->count];
    for (int i = 0; i < name_sz; i++) {
        m->name[i] = name[i];
    }
    m->name[name_sz] = 0;
    m->name_sz = name_sz;
    m->argc = 0;
    m->body = body;
    m->body_sz = body_sz;
    mt->count++;

    return m;
}

const macro* mt_find(const macro_table* mt, const char* name, int name_sz) {
    for (int i = 0; i < mt->count; i++) {
        if (same_name(mt->m[i].name, mt->m[i].name_sz, name, name_sz)) {
            return &mt->m[i];
        }
    }

    return NULL;
}

int mt_expand(const macro* m, const char argv[][MACRO_ARG_MAX], const int* argl,
              int argc, char* out, int max) {
    int n = 0;
    int i = 0;

    while (i < m->body_sz) {
        /* Only a run that starts a name can be an argument, so a digit in the
         * middle of one is never mistaken for the start of a match. */
        if (!name_char(m->body[i]) || (i > 0 && name_char(m->body[i - 1]))) {
            if (n >= max) {
                return -1;
            }
            out[n++] = m->body[i++];
            continue;
        }

        int run = 0;
        while (i + run < m->body_sz && name_char(m->body[i + run])) {
            run++;
        }

        int which = -1;
        for (int a = 0; a < m->argc && a < argc; a++) {
            if (same_name(&m->body[i], run, m->args[a], m->arg_sz[a])) {
                which = a;
                break;
            }
        }

        if (which < 0) {
            if (n + run > max) {
                return -1;
            }
            for (int k = 0; k < run; k++) {
                out[n++] = m->body[i + k];
            }
        } else {
            if (n + argl[which] > max) {
                return -1;
            }
            for (int k = 0; k < argl[which]; k++) {
                out[n++] = argv[which][k];
            }
        }
        i += run;
    }

    return n;
}
