/*
 * Host tests for .include.
 *
 * The end of an included file is the one piece of real logic in next(): it
 * restores the suspended source, puts the scope back, and synthesises a
 * newline, because an included file's last line usually has none and without
 * one it runs on into the line after the .include.
 *
 * There were no tests for any of that -- only 21 of the 247 corpus sources use
 * .include at all, and none of them pin the seam.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zap.h"

static int failures = 0;

static void check(const char* name, bool ok) {
    if (ok) {
        fprintf(stderr, "PASS  %s\n", name);
    } else {
        fprintf(stderr, "FAIL  %s\n", name);
        failures++;
    }
}

/* Writes `text` to `path`, which the tests keep in the working directory
 * because an .include resolves relative to it. */
static bool put(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    const size_t n = strlen(text);

    return fwrite(text, 1, n, f) == n && fclose(f) == 0;
}

static const char* hex(const zap_result* r) {
    static char out[256];
    int n = 0;
    out[0] = 0;
    for (int i = 0; i < r->size && n < (int) sizeof(out) - 4; i++) {
        n += snprintf(&out[n], sizeof(out) - n, "%s%02X", i ? " " : "", r->bytes[i]);
    }

    return out;
}

static void is(const char* name, const char* src, const char* want) {
    zap_result r;
    const char* got = "ERR";
    if (zap_assemble_mem(src, (int) strlen(src), "top.s", &r) && r.ok) {
        got = hex(&r);
    }
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-46s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-46s got %s, want %s\n", name, got, want);
        failures++;
    }
    zap_free(&r);
}

int main(void) {
    check("scratch files written",
          put("zt_a.inc", "  ld a,b\n")
       && put("zt_nonl.inc", "  ld a,b")          /* no trailing newline */
       && put("zt_outer.inc", "  nop\n  include \"zt_a.inc\"\n  nop\n")
       && put("zt_lbl.inc", "sub1:\n  ret\n")
       && put("zt_local.inc", "  jr @next\n@next:\n  nop\n")
       && put("zt_bad.inc", "  ld a,\n"));

    /* The ordinary case: the text reads as if it had been written in place. */
    is("included text lands in order",
       "  nop\n  include \"zt_a.inc\"\n  ret\n", "00 78 C9");

    /* The seam this exists for. The included file's last line has no newline,
     * so without a synthesised one its final token runs into the line after
     * the .include and the two are parsed as one statement. */
    is("include whose last line has no newline",
       "  nop\n  include \"zt_nonl.inc\"\n  ret\n", "00 78 C9");

    /* Nested, so the stack is popped more than once. */
    is("nested includes",
       "  include \"zt_outer.inc\"\n  ret\n", "00 78 00 C9");

    /* A label defined in an included file is visible after it, and one
     * defined before is visible inside: an include shares the enclosing
     * scope, unlike a macro expansion. */
    is("label from an included file resolves after it",
       "  .assume adl=1\n  .org 0\n  include \"zt_lbl.inc\"\n  call sub1\n",
       "C9 CD 00 00 00");

    /* Local labels inside an include belong to the enclosing scope, and the
     * scope has to be restored when it ends or the @loop before an include
     * and the one after it become different symbols. */
    is("locals survive an include",
       "  .assume adl=1\n  .org 0\n@here:\n  include \"zt_local.inc\"\n  jr @here\n",
       "18 00 00 18 FB");

    /* An error inside an included file is reported against that file and its
     * line, not the line of the .include. */
    {
        zap_result r;
        const char* src = "  nop\n  include \"zt_bad.inc\"\n  ret\n";
        const bool failed = !zap_assemble_mem(src, (int) strlen(src), "top.s", &r)
                         || !r.ok;
        check("an error inside an include is reported", failed && r.ndiags == 1);
        if (r.ndiags == 1) {
            check("  named against the included file",
                  strcmp(r.diags[0].file, "zt_bad.inc") == 0);
            check("  at its own line number", r.diags[0].line == 1);
            fprintf(stderr, "      -> %s line %d: %s\n",
                    r.diags[0].file, r.diags[0].line, r.diags[0].msg);
        }
        zap_free(&r);
    }

    /* An include on the last line of a file that has no trailing newline.
     *
     * parse_include switches the lexer to the new file before returning, and
     * the end-of-line check in the parse loop then runs against it -- so it
     * read the *included* file's first token and rejected it. A line ending
     * in a newline never showed this, because the newline is already in hand
     * and the check reads nothing.
     *
     * ZINC in the full corpus ends `zinc-setup.asm` exactly this way, and it
     * took the whole program out with an error attributed to the included
     * file at the including file's line number. */
    is("include on the last line, no trailing newline",
       "  nop\n  include \"zt_a.inc\"", "00 78");

    /* And the same one file down, where the include ends an included file. */
    check("nested scratch written",
          put("zt_tail.inc", "  ld b,c\n  include \"zt_a.inc\""));
    is("include ending an included file, no trailing newline",
       "  nop\n  include \"zt_tail.inc\"\n  ret\n", "00 41 78 C9");

    /* A missing file is an error rather than an empty include. */
    is("missing include file", "  include \"zt_no_such.inc\"\n", "ERR");

    /* Nesting deeper than the stack allows is refused rather than
     * overrunning it. Each file includes the next. */
    {
        char path[32];
        char body[64];
        bool wrote = true;
        for (int i = 0; i < 12; i++) {
            snprintf(path, sizeof(path), "zt_deep%d.inc", i);
            snprintf(body, sizeof(body), "  include \"zt_deep%d.inc\"\n", i + 1);
            wrote = wrote && put(path, body);
        }
        is("nesting deeper than the stack", "  include \"zt_deep0.inc\"\n", "ERR");
        for (int i = 0; i < 12; i++) {
            snprintf(path, sizeof(path), "zt_deep%d.inc", i);
            unlink(path);
        }
        check("deep scratch written", wrote);
    }

    unlink("zt_a.inc");
    unlink("zt_nonl.inc");
    unlink("zt_outer.inc");
    unlink("zt_lbl.inc");
    unlink("zt_local.inc");
    unlink("zt_bad.inc");
    unlink("zt_tail.inc");

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
