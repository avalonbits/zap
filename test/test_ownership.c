/*
 * The output buffer changes hands rather than being copied.
 *
 * finish() used to malloc a second copy of the whole program while the
 * parser's buffer was still alive, so a 96.8 KB output cost 96.8 KB to
 * duplicate on top of the 128 KB already holding it. The parser now gives the
 * buffer up, which means two things have to hold: the bytes must still be
 * readable after the parser is destroyed, and the parser must not free what it
 * no longer owns. ASan catches the second as a double free or a
 * use-after-free, so these run under it (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
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

/* Four bytes of output, from a source with no forward references. */
static const char* SRC =
    "  .assume adl=1\n"
    "  .org $40000\n"
    "  nop\n"
    "  nop\n"
    "  nop\n"
    "  ret\n";

int main(void) {
    /* The parser hands the buffer over and stops owning it. Reading the bytes
     * after pr_destroy is the whole point: if the parser still freed them this
     * is a use-after-free, and if it freed them twice ASan says so. */
    parser p;
    check("pr_init_mem", pr_init_mem(&p, SRC, (int) strlen(SRC), "<test>") != NULL);
    check("parse", pr_parse(&p) == NULL);

    int sz = 0;
    uint8_t* taken = pr_take_buf(&p, &sz);
    check("took four bytes", sz == 4);
    check("buffer handed over", taken != NULL);

    /* A second take finds nothing left: the parser really gave it up. */
    int again = -1;
    check("nothing left to take", pr_take_buf(&p, &again) == NULL && again == 0);

    pr_destroy(&p);

    check("bytes outlive the parser",
          taken != NULL && taken[0] == 0x00 && taken[1] == 0x00 &&
          taken[2] == 0x00 && taken[3] == 0xC9);
    free(taken);

    /* An empty output frees the buffer rather than handing back something the
     * caller then has to know is zero bytes long. */
    parser e;
    check("pr_init_mem empty", pr_init_mem(&e, "  .assume adl=1\n", 15, "<test>") != NULL);
    check("parse empty", pr_parse(&e) == NULL);
    int esz = -1;
    check("empty output hands back nothing",
          pr_take_buf(&e, &esz) == NULL && esz == 0);
    pr_destroy(&e);

    /* And the same through the public interface, which is what main.c uses. */
    zap_result r;
    check("assemble from memory", zap_assemble_mem(SRC, (int) strlen(SRC), "<test>", &r));
    check("four bytes out", r.size == 4);
    check("last byte is ret", r.size == 4 && r.bytes[3] == 0xC9);
    zap_free(&r);

    return failures == 0 ? 0 : 1;
}
