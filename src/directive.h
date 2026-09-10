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

#ifndef DIRECTIVE_H
#define DIRECTIVE_H

#include "zap.h"
#include "scan.h"

/* Inlined into callers in other files, so the bodies live here. */

static inline const char* lit_value(const char* p, const char* e, evalue* out) {
    const char* q = p;

    /* A sign, which the operand parser has always taken and these did not.
     * `DB 1, 2, 3, -1` sends one item in four back to the evaluator without
     * it, and a table of signed bytes is what DB is for. */
    bool neg = false;
    if (*q == '-' || *q == '+') {
        neg = *q == '-';
        q++;
    }
    if (!digit_ch(*q)) {
        return NULL;
    }

    const char* const d = q;
    while (q < e && num_ch(*q)) {
        q++;
    }
    const int nn = (int) (q - d);

    /* What ended the run has to end the item too. */
    const char* r = q;
    while (r < e && is_space_ch(*r)) {
        r++;
    }
    if (*r != ',' && *r != '\n' && *r != ';' && r < e) {
        return NULL;
    }

    evalue value = 0;
    int hv = 0;
    bool got;
    if (nn >= 3 && d[0] == '0' && (d[1] | 0x20) == 'x') {
        got = hex_digits(d + 2, nn - 2, &hv);
        value = hv;
    } else if (nn >= 2 && (d[nn - 1] | 0x20) == 'h') {
        got = hex_digits(d, nn - 1, &hv);
        value = hv;
    } else {
        /* First digit outside the loop, as the operand parser does it: a
         * one-digit value then needs no multiply, and `d * 10` is a call to
         * __imulu here.
         *
         * Six digits and no more, which is the same rule hex_digits keeps and
         * for the same reason: 999,999 is inside the machine's word, so `acc`
         * is an `int` and `acc * 10` is __imulu rather than the wider helper.
         * A longer run is declined and num_parse reads it. Seven digits is
         * 9,999,999 and the word holds 8,388,607, so seven is already too
         * many. */
        if (nn > 6) {
            return NULL;
        }
        int acc = d[0] - '0';
        unsigned k = 1;
        for (; k < (unsigned) nn; k++) {
            if (!digit_ch(d[k])) {
                break;
            }
            acc = acc * 10 + (d[k] - '0');
        }
        got = k == (unsigned) nn;
        value = acc;
    }
    if (!got) {
        return NULL;
    }

    *out = neg ? -value : value;

    return q;
}

#endif /* DIRECTIVE_H */
