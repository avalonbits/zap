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

#include "macro.h"
#include "parser.h"
#include "zap.h"

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

/* Checks the diagnostic rather than the bytes: several failures used to share
 * one message, so the message is worth pinning. */
static void msg_is(const char* name, const char* src, const char* want) {
    zap_result r;
    const bool failed = !zap_assemble_mem(src, (int) strlen(src), "m", &r)
                     || !r.ok;
    const char* got = (failed && r.ndiags == 1) ? r.diags[0].msg : "<none>";
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-42s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-42s got \"%s\", want \"%s\"\n", name, got, want);
        failures++;
    }
    zap_free(&r);
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

    /* The table holds MACRO_MAX macros and refuses the next one. Macros are
     * allocated as they are defined now rather than reserved in a fixed
     * array, so this exercises the allocation, the limit and the cleanup --
     * the last of which ASan checks by running at all. */
    {
        char src[MACRO_MAX * 40 + 64];
        int n = 0;
        for (int i = 0; i < MACRO_MAX; i++) {
            n += snprintf(&src[n], sizeof(src) - n,
                          "  macro m%d\n  nop\n  endmacro\n", i);
        }
        n += snprintf(&src[n], sizeof(src) - n, "  m0\n  m%d\n", MACRO_MAX - 1);
        is("a full table of macros", src, "00 00");

        /* One more than fits is refused rather than overrunning. This is a
         * limit zap has and the reference does not -- ez80asm assembles 65
         * macros without complaint, because it allocates them as it goes and
         * chains them. Now that zap allocates them too, MACRO_MAX is an
         * arbitrary cap rather than the size of an array, and could be raised
         * or dropped; that is a behaviour change, so it is left alone here
         * and belongs in the compatibility notes. */
        n = 0;
        for (int i = 0; i <= MACRO_MAX; i++) {
            n += snprintf(&src[n], sizeof(src) - n,
                          "  macro m%d\n  nop\n  endmacro\n", i);
        }
        is("one macro past the limit", src, "ERR");
    }

    /* The reasons mt_add can refuse are now told apart, so the message names
     * the actual problem. Out of memory cannot be provoked from a test without
     * a failing allocator, but it is the reason the distinction exists: on a
     * 512 KB machine it is reachable, and reporting it as a duplicate sends a
     * user hunting for a mistake in their source that is not there. The three
     * that can be provoked are checked here. */
    {
        char src[MACRO_MAX * 40 + 64];
        int n = 0;
        for (int i = 0; i <= MACRO_MAX; i++) {
            n += snprintf(&src[n], sizeof(src) - n,
                          "  macro m%d\n  nop\n  endmacro\n", i);
        }
        msg_is("a full table says so", src, "too many macros");
        msg_is("a repeated name says so",
               "  macro m\n  nop\n  endmacro\n  macro m\n  nop\n  endmacro\n",
               "duplicate macro");
    }

    /* Invocation is case-insensitive, and so is the first-character reject that
     * decides whether the table is worth scanning at all.
     *
     * mt_find is called for every identifier that begins a statement, and
     * almost none of them name a macro; one indexed load on the lower-cased
     * first character answers that without touching a macro. It has to fold
     * the same way on both sides -- a macro recorded under 'M' and looked up
     * under 'm' is invisible, and the failure is silent: the invocation is
     * left as an unknown instruction rather than reported as a missing macro.
     * These invoke in a different case from the definition in both
     * directions. */
    is("macro defined upper, invoked lower",
       "  macro MyMacro\n  ld a,b\n  endmacro\n  mymacro\n",
       "78");
    is("macro defined lower, invoked upper",
       "  macro lowmac\n  ld a,c\n  endmacro\n  LOWMAC\n",
       "79");
    is("macro defined mixed, invoked mixed differently",
       "  macro fOoBaR\n  ld a,d\n  endmacro\n  FoObAr\n",
       "7A");

    /* The duplicate check goes through the same path, so a name that differs
     * only in case is still a duplicate. */
    msg_is("a repeated name in another case says so",
           "  macro Dup\n  nop\n  endmacro\n  macro dUP\n  nop\n  endmacro\n",
           "duplicate macro");

    /* An identifier sharing no first character with any macro must still reach
     * the instruction it actually is, rather than being swallowed. */
    is("a non-macro identifier is unaffected",
       "  macro zzz\n  nop\n  endmacro\n  ld a,b\n",
       "78");

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
