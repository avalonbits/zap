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
#include "scan.h"
#include "insn.h"

uint8_t cclass[256];

/* Nibble value of a hex digit, 0xFF for anything else. Used both to test a
 * digit and to convert it, so the character is classified once. */
uint8_t hexval[256];

void build_cclass(void) {
    for (int i = 0; i < 256; i++) {
        hexval[i] = 0xFF;
    }
    for (int i = 0; i < 10; i++) {
        hexval['0' + i] = (uint8_t) i;
    }
    for (int i = 0; i < 6; i++) {
        hexval['a' + i] = (uint8_t) (10 + i);
        hexval['A' + i] = (uint8_t) (10 + i);
    }
    for (int i = 0; i < 256; i++) {
        cclass[i] = 0;
    }
    cclass[(uint8_t) ' '] |= C_SPACE;
    cclass[(uint8_t) '\t'] |= C_SPACE;
    cclass[(uint8_t) '\r'] |= C_SPACE;

    for (int c = 'a'; c <= 'z'; c++) {
        cclass[c] |= C_NAME | C_NUM;
        cclass[c - 32] |= C_NAME | C_NUM;
    }
    for (int c = '0'; c <= '9'; c++) {
        cclass[c] |= C_NAME | C_DIGIT | C_NUM;
    }
    cclass[(uint8_t) '_'] |= C_NAME;

    /* A label may contain both, and an operand naming one has to scan the
     * whole of it. `_` had C_NAME but not C_NUM, so the literal scan -- which
     * is where a name that is not a register ends up -- stopped at the first
     * underscore and the rest of the line looked like trailing text. Real
     * labels are full of them. */
    cclass[(uint8_t) '_'] |= C_NUM;
    cclass[(uint8_t) '.'] |= C_NAME | C_NUM;

    /* The at sign, which marks a local label. The reference allows it anywhere
     * in a name -- `ab@cd:` is a global there -- and only a leading one makes
     * a label local, so it is an ordinary name character here too and the
     * leading position is what parse_operand and the definition path test. */
    cclass[(uint8_t) '@'] |= C_NAME | C_NUM;

    /* A mnemonic runs over its suffix too, so the dot belongs to the same run
     * -- asking for it separately made the scan two tests per character. */
    for (int i = 0; i < 256; i++) {
        if (cclass[i] & C_NAME) {
            cclass[i] |= C_MNEM;
        }
    }
    cclass[(uint8_t) '.'] |= C_MNEM;
    cclass[(uint8_t) ','] |= C_OPEND;
    cclass[(uint8_t) '\n'] |= C_OPEND;
    cclass[(uint8_t) ';'] |= C_OPEND;
    cclass[(uint8_t) '('] |= C_LPAREN;
    for (int i = 0; i < 256; i++) {
        if ((cclass[i] & C_NAME) != 0 && (cclass[i] & C_DIGIT) == 0) {
            cclass[i] |= C_ALPHA;
        }
    }
    /* The dot is a name character but must not start one: `.5` is not a label
     * and a mnemonic suffix is scanned as part of the mnemonic. */
    cclass[(uint8_t) '.'] &= (uint8_t) ~C_ALPHA;

    /* Nor may the at sign, and here that is a saving rather than a rule: an
     * operand starting with one is a local label and cannot be a register, so
     * leaving it out of C_ALPHA sends it straight to the path that reads a
     * name and looks it up, past reg_of_text entirely. */
    cclass[(uint8_t) '@'] &= (uint8_t) ~C_ALPHA;
    for (int i = 0; i < 256; i++) {
        exop[i] = 0;
    }
    {
        const char* o = "+-*/<>&|^";
        for (int i = 0; o[i] != 0; i++) {
            exop[(uint8_t) o[i]] = 1;
        }
    }
    for (int i = 0; i < 256; i++) {
        exprec[i] = 0;
    }
    if (compat_ez80) {
        /* One level, so the climb below folds left to right and agrees with
         * the reference. */
        const char* o = "+-*/<>&|^";
        for (int i = 0; o[i] != 0; i++) {
            exprec[(uint8_t) o[i]] = 1;
        }
    } else {
        exprec[(uint8_t) '*'] = 6;
        exprec[(uint8_t) '/'] = 6;
        exprec[(uint8_t) '+'] = 5;
        exprec[(uint8_t) '-'] = 5;
        exprec[(uint8_t) '<'] = 4;   /* << */
        exprec[(uint8_t) '>'] = 4;   /* >> */
        exprec[(uint8_t) '&'] = 3;
        exprec[(uint8_t) '^'] = 2;
        exprec[(uint8_t) '|'] = 1;
    }

    cclass[(uint8_t) '$'] |= C_NUM;
    cclass[(uint8_t) '#'] |= C_NUM;
    cclass[(uint8_t) '%'] |= C_NUM;
}

/* One operand.
 *
 * A whole expression evaluator is a feature: this takes a register, a flag, a
 * literal, or a parenthesised form of those with an optional displacement, and
 * nothing else. Arithmetic between literals is one of the things to add back
 * and price later.
 */
/* An operand with nothing in it, to copy from.
 *
 * Clearing ten fields by hand is ten stores, twice for every line in the
 * source -- and two of them are the four-byte immediate and displacement,
 * which on a 24-bit machine are not one store each. Copying a prepared struct
 * lets the compiler move it in whatever way suits, and says once what "empty"
 * means instead of in three places that have to agree. */
const dop dop_none = {
    /* By name, not by position. Adding a field to dop silently shifted every
     * value after it once already: noreg took indirect's initialiser and the
     * operand came out claiming to hold a register. Nothing about that is
     * visible at the point of the mistake. */
    .r0 = 0, .r1 = 0, .r2 = 0,
    .noreg = 1,
    .reg_index = 0,
    .cc = false,
    .cc_index = 0,
    .mode = NOREQ,
    .disp = 0,
    .fwd = NULL,
    .fwd2 = NULL,
    .fwd2_neg = false,
    .imm = 0,
};

/* Whether a token could be a number at all, decided on two characters.
 *
 * Every radix this assembler takes is marked at one end or the other: a
 * leading digit for decimal, 0x and 0b; a leading $, # or %; a trailing h or
 * b. A token with none of those is a name.
 *
 * It is only a filter, and what it lets through still has to be parsed: a
 * leading digit does not make a number, since 2 is not a binary digit, so `2b`
 * is a name and so are `1z`, `5g` and `123abc`. The reference takes all four
 * as labels.
 *
 * What the filter buys is the common path -- an ordinary label neither starts
 * with a digit nor ends in h or b, so it never reaches the general parser. */
static inline bool maybe_numeric(const char* s, int n) {
    const char f = s[0];
    const char last = (char) (s[n - 1] | 0x20);

    return digit_ch(f) || f == '$' || f == '#' || f == '%'
           || last == 'h' || last == 'b';
}

bool numeric_token(const char* s, int n) {
    if (!maybe_numeric(s, n)) {
        return false;
    }

    /* The fast reader first, and num_parse for whatever it declines -- falling
     * *through* rather than returning the fast reader's answer.
     *
     * hex_digits declines a run wider than the machine's word, and returning
     * that as "not a number" would make the operand parser take `0x55555555`
     * for a label and turn it into a forward reference nothing ever defines.
     * A fast path declining must mean "ask the slow one", never "no". */
    int v = 0;
    if (n >= 3 && s[0] == '0' && (s[1] | 0x20) == 'x') {
        if (hex_digits(s + 2, n - 2, &v)) {
            return true;
        }
    } else if (n >= 2 && (s[n - 1] | 0x20) == 'h') {
        if (hex_digits(s, n - 1, &v)) {
            return true;
        }
    }
    value gv = 0;

    return num_parse(s, n, &gv);
}
