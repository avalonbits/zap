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

#include "zap.h"

#include <stdlib.h>

#include "parser.h"

static void clear(zap_result* out) {
    out->ok = false;
    out->bytes = NULL;
    out->size = 0;
    out->origin = 0;
    out->ndiags = 0;
}

/* Runs a parser that has already been set up, and copies what it produced out
 * of it so the caller owns it. */
static bool finish(parser* p, zap_result* out) {
    const char* err = pr_parse(p);

    if (err != NULL && *err != 0) {
        if (p->has_diag_) {
            out->diags[0] = p->diag_;
            out->ndiags = 1;
        }
        pr_destroy(p);

        return false;
    }

    int sz = 0;
    const uint8_t* buf = pr_buf(p, &sz);

    if (sz > 0) {
        out->bytes = (uint8_t*) malloc((size_t) sz);
        if (out->bytes == NULL) {
            pr_destroy(p);

            return false;
        }
        for (int i = 0; i < sz; i++) {
            out->bytes[i] = buf[i];
        }
    }
    out->size = sz;
    out->origin = p->start_;
    out->ok = true;

    pr_destroy(p);

    return true;
}

bool zap_assemble_file(const char* path, zap_result* out) {
    clear(out);

    parser p;
    if (pr_init(&p, path) == NULL) {
        return false;
    }

    return finish(&p, out);
}

bool zap_assemble_mem(const char* text, int len, const char* name,
                      zap_result* out) {
    clear(out);

    parser p;
    if (pr_init_mem(&p, text, len, name) == NULL) {
        return false;
    }

    return finish(&p, out);
}

void zap_free(zap_result* out) {
    free(out->bytes);
    out->bytes = NULL;
    out->size = 0;
    out->ndiags = 0;
    out->ok = false;
}
