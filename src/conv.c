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

#include "conv.h"

#include <stdbool.h>
#include <stdint.h>

static void reverse(char* buf, int sz) {
    int start = 0;
    int end = sz -1;
    while (start < end) {
        char ch = buf[start];
        buf[start] = buf[end];
        buf[end] = ch;
        ++start;
        --end;
    }
}

const char table[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};


static char* n2s(int num, char* buf, int sz, const int base) {
    const bool is_neg = num < 0;
    if (is_neg) {
        num = -num;
    }

    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    int i = 0;
    while (num != 0 && i < sz) {
        int rem = num % base;
        buf[i++] = table[rem];
        num = num / base;
    }

    if (num != 0) {
        // Not enough buffer space.
        return NULL;
    }
    if (is_neg) {
        if (i > sz-2) {
            // Not enough space for negative sign.
            return NULL;
        }
        buf[i++] = '-';
    }

    reverse(buf, i);
    buf[i] = 0;
    return buf;

}

char* i2s(int num, char* buf, int sz) {
    return n2s(num, buf, sz, 10);
}

char* h2s(int num, char* buf, int sz) {
    return n2s(num, buf, sz, 16);
}
