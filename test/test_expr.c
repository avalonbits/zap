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

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
