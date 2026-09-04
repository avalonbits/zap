/*
 * Host tests for macros and conditional assembly.
 *
 * Expected bytes come from ez80asm. The cases worth pinning are the scoping
 * ones: a macro body gets a fresh local scope per expansion, and that scope
 * has to end with the expansion, or a routine that calls a macro halfway
 * through has its own local labels split into two symbols.
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
    char path[] = "/tmp/zap_mac_XXXXXX";
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

static void is(const char* name, const char* src, const char* want) {
    const char* got = emit(src);
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-42s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-42s got %s, want %s\n", name, got, want);
        failures++;
    }
}

int main(void) {
    /* A macro with no arguments, invoked twice. */
    is("argumentless macro",
       "  macro two\n  ld a,b\n  ld a,c\n  endmacro\n  two\n  two\n",
       "78 79 78 79");

    /* Substitution is textual, so an argument can be a register as readily as
     * a number -- which is why the body is kept as text rather than tokens. */
    is("macro with arguments",
       "  macro mv arg1, arg2\n  ld a, arg1\n  ld l, arg2\n  endmacro\n  mv 10,15\n",
       "3E 0A 2E 0F");
    is("macro passing registers",
       "  macro mv arg1, arg2\n  ld a, arg1\n  ld l, arg2\n  endmacro\n  mv b,c\n",
       "78 69");

    /* An argument name is replaced only where it stands as a whole word, so a
     * macro argument called "one" must not corrupt a label called "oneshot". */
    is("argument does not match inside a word",
       "oneshot: equ 7\n  macro m one\n  db one, oneshot\n  endmacro\n  m 3\n",
       "03 07");

    /* The terminator may be written with or without the leading dot. */
    is("dotted endmacro",
       "  .macro one\n  ld a,b\n  .endmacro\n  one\n",
       "78");

    /* Each expansion gets its own local scope, so a label in the body does
     * not collide with the previous invocation's. */
    is("locals are fresh per expansion",
       "  macro loop\n@l:\n  djnz @l\n  endmacro\n  loop\n  loop\n",
       "10 FE 10 FE");

    /* And that scope has to end with the expansion. Without restoring it, the
     * @done defined before the macro call and the one referenced after it are
     * two different symbols, inside one routine -- which is what broke
     * snes.asm. */
    is("expansion does not split the caller's locals",
       "  macro nop2\n  nop\n  nop\n  endmacro\nrtn:\n@done:\n  nop2\n  jp @done\n",
       "00 00 C3 00 00 04");

    /* A macro body may only define local labels: a global or anonymous one
     * would be redefined on every invocation. */
    is("global label in a macro body",
       "  macro m\nlbl:\n  nop\n  endmacro\n  m\n", "ERR");
    is("anonymous label in a macro body",
       "  macro m\n@@:\n  nop\n  endmacro\n  m\n", "ERR");

    /* Macro errors the reference rejects. */
    is("macro without endmacro",  "  macro m\n  ld a,b\n",              "ERR");
    is("numeric argument name",   "  macro m 1\n  ld a,1\n  endmacro\n", "ERR");
    is("reserved argument name",  "  macro m and\n  nop\n  endmacro\n",  "ERR");
    is("wrong argument count",
       "  macro m a1, a2\n  db a1, a2\n  endmacro\n  m 1\n", "ERR");

    /* Conditionals. */
    is("if taken",     "  if 1\n  ld a,b\n  endif\n",                    "78");
    is("if not taken", "  if 0\n  ld a,b\n  endif\n",                    "");
    is("if else, taken",
       "  if 1\n  ld a,b\n  else\n  ld a,c\n  endif\n", "78");
    is("if else, not taken",
       "  if 0\n  ld a,b\n  else\n  ld a,c\n  endif\n", "79");
    is("condition over a constant",
       "T: equ 1\n  if T\n  ld a,b\n  else\n  ld a,c\n  endif\n", "78");

    /* A nested if inside a skipped branch still has to be counted, or its
     * endif closes the outer one early and the rest of the file is dropped. */
    is("nested if inside a skipped branch",
       "  if 0\n  if 1\n  ld a,b\n  endif\n  endif\n  ld a,c\n", "79");
    is("nested if inside a taken branch",
       "  if 1\n  if 0\n  ld a,b\n  endif\n  ld a,c\n  endif\n", "79");

    /* An else inside a branch that is already being skipped must not turn
     * assembly back on. Only the nested conditional's own state may change,
     * or the inner else branch gets assembled from inside a false outer one.
     * The earlier nested tests had no else, which is why they passed. */
    is("else nested in a false branch",
       "  if 0\n  if 0\n  ld a,b\n  else\n  ld a,c\n  endif\n  endif\n  nop\n",
       "00");
    is("else nested in a taken branch",
       "  if 1\n  if 0\n  ld a,b\n  else\n  ld a,c\n  endif\n  endif\n  nop\n",
       "79 00");

    /* An over-long argument is reported, not trimmed: a truncated one expands
     * into something that still looks like source. */
    is("macro argument too long",
       "  macro m a\n  db a\n  endmacro\n"
       "  m XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n",
       "ERR");

    /* Unbalanced conditionals. */
    is("if without endif",   "  if 1\n  ld a,b\n",  "ERR");
    is("endif without if",   "  endif\n",           "ERR");
    is("else without if",    "  else\n",            "ERR");

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
