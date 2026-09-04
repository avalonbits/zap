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

#include "value.h"

static char lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch + 0x20;
    }

    return ch;
}

static bool digit_val(char ch, int base, int* out) {
    int v;
    if (ch >= '0' && ch <= '9') {
        v = ch - '0';
    } else {
        const char lc = lower(ch);
        if (lc < 'a' || lc > 'f') {
            return false;
        }
        v = lc - 'a' + 10;
    }
    if (v >= base) {
        return false;
    }
    *out = v;

    return true;
}

/* Accumulates digits in the given base. An empty run is a valid zero: the
 * reference assembler reads '0b' as binary with no digits, and emits 0.
 *
 * A value too wide for the eZ80 wraps rather than failing. That is deliberate:
 * the reference does not treat an oversized literal as a literal error, it
 * lets the operand's own range check catch it, and its own number tests rely
 * on that ("test overflow test in operand parsing"). Accumulating unsigned
 * keeps the wraparound defined instead of relying on signed overflow. */
static bool scan_base(const char* txt, int sz, int base, value* out) {
    uint32_t acc = 0;
    for (int i = 0; i < sz; i++) {
        int d;
        if (!digit_val(txt[i], base, &d)) {
            return false;
        }
        acc = acc * (uint32_t) base + (uint32_t) d;
    }
    *out = (value) acc;

    return true;
}

/* Whether the text could possibly be a literal, decided from its first and
 * last character alone.
 *
 * Every identifier is offered to num_parse before it is looked up as a name,
 * and on real sources around 90% of those are names that fail only after
 * scan_base has walked the whole string. The forms below mirror num_parse's
 * own branches exactly: a prefix, a lone character, an 'h' or 'b' suffix, or
 * a bare decimal -- and a bare decimal has to start with a digit or scan_base
 * rejects it. Anything else cannot parse, so it is not worth scanning.
 *
 * Letter-initial hex is why this tests the last character rather than just
 * the first: C0h and Ah are literals the reference accepts and its corpus
 * pins. */
static bool could_be_number(const char* txt, int sz) {
    const char first = txt[0];
    if (first == '$' || first == '#' || first == '%') {
        return true;
    }
    if (sz == 1 || (first >= '0' && first <= '9')) {
        return true;
    }

    const char last = lower(txt[sz - 1]);

    return last == 'h' || last == 'b';
}

bool num_parse(const char* txt, int sz, value* out) {
    if (sz <= 0) {
        return false;
    }
    if (!could_be_number(txt, sz)) {
        return false;
    }

    /* Prefixed forms. A bare '$' is the current program counter, which only
     * the expression evaluator can supply, so it is not a literal here. */
    if (txt[0] == '$') {
        return sz > 1 && scan_base(&txt[1], sz - 1, 16, out);
    }
    if (txt[0] == '#') {
        return sz > 1 && scan_base(&txt[1], sz - 1, 16, out);
    }
    if (txt[0] == '%') {
        return sz > 1 && scan_base(&txt[1], sz - 1, 2, out);
    }

    /* A lone character is always decimal. Without this the register names
     * a..f would each parse as a hex digit. */
    if (sz == 1) {
        return scan_base(txt, 1, 10, out);
    }

    /* The 'h' suffix is claimed before the '0b' prefix, so 0bh is hex 0x0B
     * and 0b1h is hex 0xB1. Matching that order is what makes the reference
     * assembler's number tests come out byte for byte. */
    const char last = lower(txt[sz - 1]);
    if (last == 'h') {
        return scan_base(txt, sz - 1, 16, out);
    }

    if (txt[0] == '0') {
        const char second = lower(txt[1]);
        if (second == 'x') {
            return scan_base(&txt[2], sz - 2, 16, out);
        }
        if (second == 'b') {
            return scan_base(&txt[2], sz - 2, 2, out);
        }
    }

    if (last == 'b') {
        return scan_base(txt, sz - 1, 2, out);
    }

    return scan_base(txt, sz, 10, out);
}

bool num_is_literal(const char* txt, int sz) {
    value ignored;

    return num_parse(txt, sz, &ignored);
}

bool esc_char(char ch, char* out) {
    switch (ch) {
        case 'a':  *out = 0x07; return true;
        case 'b':  *out = 0x08; return true;
        case 'e':  *out = 0x1B; return true;
        case 'f':  *out = 0x0C; return true;
        case 'n':  *out = 0x0A; return true;
        case 'r':  *out = 0x0D; return true;
        case 't':  *out = 0x09; return true;
        case 'v':  *out = 0x0B; return true;
        case '\\': *out = 0x5C; return true;
        case '\'': *out = 0x27; return true;
        case '"':  *out = 0x22; return true;
        case '?':  *out = 0x3F; return true;
        case '0':  *out = 0x00; return true;
        default:   return false;
    }
}
