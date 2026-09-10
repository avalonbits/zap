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

#ifndef SCAN_H
#define SCAN_H

#include "zap.h"

/* Inlined into callers in other files, so the bodies live here. */

static inline bool is_space_ch(char c) {
    return (cclass[(uint8_t) c] & C_SPACE) != 0;
}

static inline bool name_ch(char c) {
    return (cclass[(uint8_t) c] & C_NAME) != 0;
}

static inline bool num_ch(char c) {
    return (cclass[(uint8_t) c] & C_NUM) != 0;
}

static inline bool alpha_ch(char c) {
    return (cclass[(uint8_t) c] & C_ALPHA) != 0;
}

static inline bool digit_ch(char c) {
    return (cclass[(uint8_t) c] & C_DIGIT) != 0;
}

/* A run of hexadecimal digits, assembled into a value.
 *
 * Read from the end, a byte at a time, rather than accumulated as
 * `acc = (acc << 4) | digit`. There is no barrel shifter here, and the
 * compiler will not turn a left shift into a byte move even at a byte
 * boundary: `<< 4` and `<< 8` are both `ld c, n; call __ishl`, a loop over the
 * bits, several hundred cycles per digit.
 *
 * Working backwards, two digits make a byte with one table lookup for the high
 * nibble, and the bytes go straight into the value's own storage. Nothing here
 * shifts anything wider than a nibble.
 *
 * Three fixed steps rather than a loop: a value is at most three bytes, so a
 * loop could only run three times and would pay for a counter, a bound and an
 * indexed store into the union.
 *
 * The digits are validated here rather than in a pass of their own. `hexval`
 * gives 0xFF for anything that is not a hex digit and a real nibble is 0x0F or
 * less, so OR-ing the nibbles together and testing the high half at the end
 * says whether any was rejected, with no branch per digit.
 *
 * Little-endian, which both the eZ80 and the host are; the emitter writes the
 * low byte first for the same reason. A run longer than the machine's word is
 * declined rather than truncated -- see the test.
 *
 * It takes the run of digits rather than a whole token, which is what lets
 * `0x1234` and `1234h` share it. */
static inline bool hex_digits(const char* d, int n, int* out) {
    /* Six digits, which is the machine's word. Anything longer is declined
     * here and read by num_parse, which works in the evaluator's wider word
     * and handles every radix.
     *
     * Keeping four bytes here so that `dw32 0x55555555` could come through
     * would widen the union and the variable it moves through for every
     * literal in the file, to serve a form that is rare. Declining costs
     * nothing: lit_value returns NULL and the caller falls through to the
     * evaluator.
     *
     * Six and not seven, because seven digits is 28 bits and does not fit
     * either. */
    if (n > 6) {
        return false;
    }

    union {
        int v;
        uint8_t b[sizeof(int)];
    } u;
    u.v = 0;

    uint8_t bad = 0;
    int j = n;

    if (j > 0) {
        uint8_t c = hexval[(uint8_t) d[--j]];
        bad |= c;
        if (j > 0) {
            const uint8_t hi = hexval[(uint8_t) d[--j]];
            bad |= hi;
            /* Masked: an invalid digit reaches this before `bad` is tested,
             * and shl4 holds sixteen entries. */
            c = (uint8_t) (c | shl4[hi & 15]);
        }
        u.b[0] = c;
    }
    if (j > 0) {
        uint8_t c = hexval[(uint8_t) d[--j]];
        bad |= c;
        if (j > 0) {
            const uint8_t hi = hexval[(uint8_t) d[--j]];
            bad |= hi;
            c = (uint8_t) (c | shl4[hi & 15]);
        }
        u.b[1] = c;
    }
    if (j > 0) {
        uint8_t c = hexval[(uint8_t) d[--j]];
        bad |= c;
        if (j > 0) {
            const uint8_t hi = hexval[(uint8_t) d[--j]];
            bad |= hi;
            c = (uint8_t) (c | shl4[hi & 15]);
        }
        u.b[2] = c;
    }

    if ((bad & 0xF0) != 0) {
        return false;
    }
    *out = u.v;

    return true;
}

#endif /* SCAN_H */
