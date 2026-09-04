/*
 * Host tests for labels, scopes and addressing.
 *
 * Expected bytes come from running ez80asm on the same source, since matching
 * it is the requirement. Its default origin is 0x40000, so an address operand
 * with no .ORG reads back as xx 00 04.
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

static const char* emit(const char* src) {
    static char out[1024];
    char path[] = "/tmp/zap_sym_XXXXXX";
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

static void asm_is(const char* name, const char* src, const char* want) {
    const char* got = emit(src);
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-40s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-40s got %s, want %s\n", name, got, want);
        failures++;
    }
}

int main(void) {
    /* A forward reference leaves a hole that is filled at the end. The old
     * code appended instead of patching, so the RET here was overwritten and
     * the output came out 18 02 00. */
    asm_is("forward jr patches in place",
           "  jr fwd\nfwd:\n  ret\n",
           "18 00 C9");
    asm_is("backward jr",
           "st:\n  jr st\n",
           "18 FE");
    asm_is("forward jp",
           "  jp fwd\n  ld a,b\nfwd:\n  ret\n",
           "C3 05 00 04 78 C9");
    asm_is("backward jp",
           "back:\n  ld a,b\n  jp back\n",
           "78 C3 00 00 04");

    /* Addresses follow .ORG, and a label defined before one is not moved by
     * it. The address used to be worked out as buffer-offset plus origin at
     * the moment the label was resolved, so a later .ORG rewrote history. */
    asm_is("org sets the address",
           "  .org $B0000\nhere:\n  jp here\n",
           "C3 00 00 0B");
    asm_is("default origin is 0x40000",
           "here:\n  jp here\n",
           "C3 00 00 04");

    /* A name with a colon straight after it is a label even when it is also a
     * mnemonic, and it can be referenced as one: the reference assembles
     * "jp pea" as a jump to the label. A condition code is different -- there,
     * "jp nz" reads as a condition code with a missing address, so a label
     * called nz can be defined but not referenced this way. */
    asm_is("mnemonic as a label",
           "pea:\n  jp pea\n",
           "C3 00 00 04");

    /* The colon has to be immediate. "lbl :" is not a label in the reference
     * either -- it reports an invalid mnemonic. */
    asm_is("space before colon is not a label",
           "lbl :\n  ret\n",
           "ERR");

    /* Local labels are scoped to the enclosing global label, so the same name
     * in two routines is two symbols. */
    asm_is("locals are per scope",
           "one:\n@l:\n  jp @l\ntwo:\n@l:\n  jp @l\n",
           "C3 00 00 04 C3 04 00 04");
    asm_is("local forward reference",
           "one:\n  jp @l\n@l:\n  ret\n",
           "C3 04 00 04 C9");

    /* Anonymous labels: @b and @p look back, @f and @n look forward. */
    asm_is("anonymous backward",
           "@@:\n  jp @b\n",
           "C3 00 00 04");
    asm_is("anonymous forward",
           "  jp @f\n@@:\n  ret\n",
           "C3 04 00 04 C9");
    asm_is("anonymous backward is the nearest one",
           "@@:\n  ld a,b\n@@:\n  jp @p\n",
           "78 C3 01 00 04");
    asm_is("no anonymous label before here",
           "  jp @b\n",
           "ERR");
    asm_is("no anonymous label after here",
           "  jp @f\n",
           "ERR");

    /* equ defines the label on its own line to a value rather than an
     * address, and the value can be an expression over earlier constants. */
    asm_is("equ chain",
           "five: equ 5\nten: equ 10\ntwelve: equ ten+2\n  ld a,twelve\n",
           "3E 0C");
    /* The prescan is what makes this work: after1 is defined below the line
     * that uses it, and folding it needs no addresses. */
    asm_is("constant used before it is defined",
           "  db before, after1\nbefore: equ 1\nafter1: equ 2\n",
           "01 02");
    asm_is("equ without a label",
           "  equ 5\n",
           "ERR");

    /* '$' is the address of the statement being assembled, not of the operand
     * being read -- by the time the operand is reached the opcode has already
     * been emitted, so the two differ by one. Checked against ez80asm. */
    asm_is("dollar is the statement address",
           "  .org $B0000\n  jp $\n  ld a,b\n  jp $\n  db $\n",
           "C3 00 00 0B 78 C3 05 00 0B 09");
    asm_is("dollar in an expression",
           "  .org $B0000\n  jp $+3\n",
           "C3 03 00 0B");

    /* A source whose last line has no trailing newline. Files in the
     * reference corpus are written this way. */
    asm_is("no trailing newline",
           "back:\n  ld a,b\n  jp back",
           "78 C3 00 00 04");

    /* A label only stands as the target of an equ on its own line. Without
     * that, "foo: ld a,b" followed by a bare "equ 5" silently redefined foo,
     * and every later reference to it used 5. */
    asm_is("equ does not capture an earlier label",
           "foo: ld a,b\n  equ 5\n  ld a,foo\n",
           "ERR");
    asm_is("equ still takes the label on its own line",
           "foo: equ 5\n  ld a,foo\n",
           "3E 05");

    /* An address operand is as wide as the mode says. In Z80 mode this used
     * to emit a stray third byte and shift everything after it. */
    asm_is("z80 mode address is two bytes",
           "  .assume adl=0\n  ld hl,$1234\n  ret\n",
           "21 34 12 C9");
    asm_is("adl mode address is three bytes",
           "  .assume adl=1\n  ld hl,$1234\n  ret\n",
           "21 34 12 00 C9");

    /* Local scopes are counted in 14 bits, not 7. With a one-byte scope
     * masked to 0x7F, two routines exactly 128 global labels apart shared a
     * key and the second @l overwrote the first. */
    {
        /* The reference is forward, so it is patched at the end -- after the
         * far scope has had its chance to overwrite the key. A backward
         * reference would resolve before the collision could bite. */
        char src[8192];
        int n = snprintf(src, sizeof(src), "g0:\n  jp @l\n@l:\n  ld a,b\n");
        for (int i = 1; i <= 128; i++) {
            n += snprintf(&src[n], sizeof(src) - n, "g%d:\n", i);
        }
        snprintf(&src[n], sizeof(src) - n, "@l:\n  ld a,c\n");
        const char* got = emit(src);
        /* The @l in scope 1 is at 0x40004. A collision with scope 129 would
         * patch this to that routine's @l at 0x40005 instead. */
        const char* want = "C3 04 00 04 78 79";
        if (strcmp(got, want) == 0) {
            fprintf(stderr, "PASS  %-40s %s\n", "scopes 128 apart do not collide", got);
        } else {
            fprintf(stderr, "FAIL  %-40s got %s, want %s\n",
                    "scopes 128 apart do not collide", got, want);
            failures++;
        }
    }

    /* A relative jump out of range is reported at the reference, not silently
     * truncated. */
    {
        char src[8192];
        int n = snprintf(src, sizeof(src), "  jr fwd\n");
        for (int i = 0; i < 200; i++) {
            n += snprintf(&src[n], sizeof(src) - n, "  ld a,b\n");
        }
        snprintf(&src[n], sizeof(src) - n, "fwd:\n  ret\n");
        const char* got = emit(src);
        if (strcmp(got, "ERR") == 0) {
            fprintf(stderr, "PASS  %-40s %s\n", "relative jump too far", got);
        } else {
            fprintf(stderr, "FAIL  %-40s got %s, want ERR\n", "relative jump too far", got);
            failures++;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
