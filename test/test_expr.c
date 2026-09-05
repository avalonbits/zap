/*
 * Host tests for expression evaluation.
 *
 * These go through the whole path -- lexer, expression evaluator, db emitter --
 * and check the bytes, because bytes are what has to match the reference
 * assembler. Evaluating an expression correctly in isolation is not the goal;
 * agreeing with ez80asm is.
 *
 * The cases are taken from its Value_operators and Numbers corpora, with the
 * expected values read off the .expect binaries it ships. The ones that matter
 * most are the precedence cases: evaluation is strictly left to right, so
 * 1+2*3 is 9 and 1+2<<2 is 12. Anything that quietly introduced C precedence
 * here would still look reasonable and would emit different bytes.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"

static int failures = 0;

/* Assembles one source and renders the emitted bytes as hex. */
static const char* emit(const char* src) {
    static char out[512];
    char path[] = "/tmp/zap_expr_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return "<mkstemp failed>";
    }
    if (write(fd, src, strlen(src)) != (long) strlen(src)) {
        close(fd);
        unlink(path);

        return "<write failed>";
    }
    close(fd);

    parser p;
    if (pr_init(&p, path) == NULL) {
        unlink(path);

        return "<init failed>";
    }

    const char* err = pr_parse(&p);
    int n = 0;
    out[0] = 0;
    if (err != NULL && *err != 0) {
        n = snprintf(out, sizeof(out), "ERR");
    } else {
        int sz = 0;
        const uint8_t* buf = pr_buf(&p, &sz);
        for (int i = 0; i < sz && n < (int) sizeof(out) - 4; i++) {
            n += snprintf(&out[n], sizeof(out) - n, "%s%02X", i ? " " : "", buf[i]);
        }
    }
    out[n] = 0;

    pr_destroy(&p);
    unlink(path);

    return out;
}

/* Wraps an expression in a db and checks the byte it produces. */
static void db_is(const char* expr, const char* want) {
    char src[256];
    snprintf(src, sizeof(src), "    db %s\n", expr);
    const char* got = emit(src);
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  db %-34s %s\n", expr, got);
    } else {
        fprintf(stderr, "FAIL  db %-34s got %s, want %s\n", expr, got, want);
        failures++;
    }
}

/* The same expression resolved two ways: with the label defined after it, so
 * the text has to be kept and re-evaluated, and with the label defined before
 * it, so it resolves on the spot.
 *
 * Comparing the two paths against each other rather than against a number
 * written here is the point. A hand-computed expectation tests my arithmetic
 * as much as the assembler's -- the first draft of these cases had one wrong --
 * and what actually needs proving is that deferring an expression does not
 * change what it means. */
static void deferred_matches(const char* name, const char* expr) {
    char src[1024];
    static char immediate[1024];

    snprintf(src, sizeof(src), "LATER: equ 5\n    db %s\n", expr);
    snprintf(immediate, sizeof(immediate), "%s", emit(src));

    snprintf(src, sizeof(src), "    db %s\nLATER: equ 5\n", expr);
    const char* deferred = emit(src);

    if (strcmp(deferred, immediate) == 0 && strcmp(deferred, "ERR") != 0) {
        fprintf(stderr, "PASS  %-44s %s\n", name, deferred);
    } else {
        fprintf(stderr, "FAIL  %-44s deferred %s, immediate %s\n",
                name, deferred, immediate);
        failures++;
    }
}

int main(void) {
    /* Strictly left to right. These four are the whole reason this file
     * exists: under C precedence they would be 07, 07, 03 and 09. */
    db_is("1+2*3", "09");
    db_is("2*3+1", "07");
    db_is("8-2-3", "03");
    db_is("1+2<<2", "0C");

    /* Brackets are the only way to group. */
    db_is("1+[2*3]", "07");
    db_is("[1+2]*3", "09");
    db_is("[0]", "00");
    db_is("[ 1-1 ]", "00");
    db_is("[\t0\t]", "00");

    /* Unary operators, including one applied to a second operand. */
    db_is("-1", "FF");
    db_is("~0", "FF");
    db_is("+10", "0A");
    db_is("-10", "F6");
    db_is("~10", "F5");
    db_is("1--1", "02");
    db_is("1+~1", "FF");
    db_is("10-+1", "09");

    /* Every binary operator, from the reference's operator_*.s files. */
    db_is("10+1", "0B");
    db_is("10-1", "09");
    db_is("10*1", "0A");
    db_is("10/2", "05");
    db_is("10&2", "02");
    db_is("10|2", "0A");
    db_is("10^2", "08");
    db_is("10<<2", "28");
    db_is("10>>2", "02");

    /* The long compound from compound_all_operator_values_dx.s, whose
     * expected bytes are in that test's .expect. */
    db_is("128+127-255+4/2*2<<1>>1&0x04|0x04", "04");
    db_is("-128", "80");
    db_is("0x55^0", "55");
    db_is("0xff-0xff+1", "01");

    /* Character literals take part in expressions. */
    db_is("'a'", "61");
    db_is("'a'+1", "62");
    db_is("'\\\\'", "5C");
    db_is("'\\\\'+1", "5D");
    db_is("'\\a'", "07");
    db_is("'\\a'+1", "08");
    db_is("'\\''", "27");
    db_is("'\\''+1", "28");
    db_is("'\\'", "5C");
    db_is("'\\'+1", "5D");

    /* Literals in every base reach the same value. */
    db_is("0xC0", "C0");
    db_is("C0h", "C0");
    db_is("$C0", "C0");
    db_is("#C0", "C0");
    db_is("%11000000", "C0");
    db_is("0b11000000", "C0");
    db_is("11000000b", "C0");
    db_is("192", "C0");

    /* The right shift keeps the sign. Differential fuzzing against ez80asm
     * caught this: with a logical shift, -87>>10 is 4194302 and the whole
     * expression comes out 0x66 instead of 0x00. */
    db_is("+10-'a'>>10/10", "00");
    db_is("-C0h*C0h>>$1F&255", "FF");
    db_is("1-0x0A>>$1F|'0'/7", "00");
    /* An over-wide shift count is masked to 31, which is what the reference
     * gets from the hardware. */
    db_is("-1010b>>0xFF*99", "9D");
    db_is("-42<<#7E+255+0Ah>>'Z'", "E0");

    /* Spacing must not change the parse. */
    db_is("1 + 1", "02");
    db_is("1+ 1", "02");
    db_is("1 +1", "02");

    /* Lists, and a string alongside an expression. */
    {
        const char* got = emit("    db 'a', 'b', 0\n");
        const char* want = "61 62 00";
        if (strcmp(got, want) == 0) {
            fprintf(stderr, "PASS  %-37s %s\n", "db 'a', 'b', 0", got);
        } else {
            fprintf(stderr, "FAIL  %-37s got %s, want %s\n", "db 'a', 'b', 0", got, want);
            failures++;
        }
    }
    {
        const char* got = emit("    db \"MOS\", 0, 1\n");
        const char* want = "4D 4F 53 00 01";
        if (strcmp(got, want) == 0) {
            fprintf(stderr, "PASS  %-37s %s\n", "db \"MOS\", 0, 1", got);
        } else {
            fprintf(stderr, "FAIL  %-37s got %s, want %s\n", "db \"MOS\", 0, 1", got, want);
            failures++;
        }
    }

    /* Escapes inside a quoted string. These broke when the R register was
     * added to the reserved words: "\r" came back as a register token, and
     * the string reader was demanding a name. Every .asciz with a carriage
     * return in it stopped assembling, and only the emulator noticed. */
    {
        struct { const char* src; const char* want; } strs[] = {
            { "  .asciz \"Hi\\r\\n\"\n",  "48 69 0D 0A 00" },
            { "  .ascii \"a\\tb\"\n",       "61 09 62" },
            { "  .ascii \"q\\\\q\"\n",        "71 5C 71" },
            { "  .ascii \"\\\"\"\n",           "22" },
            { "  .ascii \"bad\\zescape\"\n", "ERR" },
        };
        for (unsigned i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
            const char* got = emit(strs[i].src);
            if (strcmp(got, strs[i].want) == 0) {
                fprintf(stderr, "PASS  %-37s %s\n", "string escape", got);
            } else {
                fprintf(stderr, "FAIL  %-37s got %s, want %s\n",
                        "string escape", got, strs[i].want);
                failures++;
            }
        }
    }

    /* Things that must not assemble. */
    db_is("''", "ERR");
    db_is("'\\+'", "ERR");
    db_is("1/0", "ERR");
    db_is("[1+2", "ERR");
    db_is("1+", "ERR");
    db_is("undefined_thing", "ERR");
    /* Only one unary operator is allowed, matching the reference's
     * "Illegal unary operator". 1--1 above is a binary minus then a unary one,
     * which is fine. */
    db_is("--1", "ERR");
    db_is("-~1", "ERR");
    db_is("~~1", "ERR");
    db_is("+-1", "ERR");
    /* Out of range truncates rather than failing -- ez80asm warns and emits
     * the low byte, and zap has to emit the same one. */
    db_is("300", "2C");
    db_is("2+7<<0Ah", "00");

    /* Arithmetic wraps instead of overflowing. Both of these tripped UBSan
     * before add/sub/mul went through unsigned; the reference wraps too and
     * emits 00 for the first. */
    db_is("0x7fffffff+1", "00");
    db_is("0xffffff+1", "00");
    db_is("0x80000000/-1", "00");

    /* Names are bounded at 64 characters, which is what the reference allows.
     * A longer one used to be copied straight into a 26-byte field; the
     * overrun stayed inside the enclosing allocation, so it corrupted the
     * next entry rather than tripping a sanitizer. */
    {
        char src[256];
        char name[80];
        for (int n = 0; n < 64; n++) {
            name[n] = 'L';
        }
        name[64] = 0;
        snprintf(src, sizeof(src), "    call %s\n%s:\n    ret\n", name, name);
        const char* got = emit(src);
        const char* want = "CD 04 00 04 C9";
        if (strcmp(got, want) == 0) {
            fprintf(stderr, "PASS  %-37s %s\n", "64-character label", got);
        } else {
            fprintf(stderr, "FAIL  %-37s got %s, want %s\n", "64-character label", got, want);
            failures++;
        }

        name[64] = 'L';
        name[65] = 0;
        snprintf(src, sizeof(src), "    call %s\n%s:\n    ret\n", name, name);
        got = emit(src);
        if (strcmp(got, "ERR") == 0) {
            fprintf(stderr, "PASS  %-37s %s\n", "65-character label rejected", got);
        } else {
            fprintf(stderr, "FAIL  %-37s got %s, want ERR\n", "65-character label", got);
            failures++;
        }
    }

    /* A deferred expression is re-evaluated from the source text it covered,
     * not from a string rebuilt out of its tokens. The span has to be exactly
     * the expression: one character short at either end changes what it means,
     * and the failure is silent -- the fixup resolves to a different number and
     * the program assembles to the wrong bytes with no error.
     *
     * The spacing cases are the ones that distinguish a span from a rebuild.
     * The old code emitted tokens separated by single spaces, so it could not
     * tell these apart; a span reproduces them exactly and has to survive
     * re-lexing anyway. */
    deferred_matches("deferred: bare forward reference", "LATER");
    deferred_matches("deferred: forward reference in a sum", "1+LATER");
    deferred_matches("deferred: left to right, not precedence", "1+LATER*2");
    deferred_matches("deferred: irregular spacing", "1  +   LATER");
    deferred_matches("deferred: no spacing at all", "1+LATER-1");
    deferred_matches("deferred: brackets", "[1+LATER]*2");
    deferred_matches("deferred: unary minus on the reference", "10+-LATER");
    deferred_matches("deferred: shift", "LATER<<2");
    deferred_matches("deferred: trailing spaces before the newline", "LATER  ");

    /* Two references in one expression, so the fixup is re-attempted and has
     * to still hold the whole span the second time. */
    {
        const char* src = "    db FIRST+SECOND\nFIRST: equ 2\nSECOND: equ 3\n";
        const char* got = emit(src);
        if (strcmp(got, "05") == 0) {
            fprintf(stderr, "PASS  %-44s %s\n", "deferred: two forward references", got);
        } else {
            fprintf(stderr, "FAIL  %-44s got %s, want 05\n",
                    "deferred: two forward references", got);
            failures++;
        }
    }

    /* The span is bounded. An expression too long to keep is refused rather
     * than truncated, which would resolve to something else entirely. */
    {
        char src[1024];
        int n = snprintf(src, sizeof(src), "    db LATER");
        for (int i = 0; i < 70; i++) {
            n += snprintf(&src[n], sizeof(src) - n, "+1");
        }
        snprintf(&src[n], sizeof(src) - n, "\nLATER: equ 5\n");
        const char* got = emit(src);
        if (strcmp(got, "ERR") == 0) {
            fprintf(stderr, "PASS  %-44s %s\n", "deferred: over-long expression refused", got);
        } else {
            fprintf(stderr, "FAIL  %-44s got %s, want ERR\n",
                    "deferred: over-long expression refused", got);
            failures++;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
