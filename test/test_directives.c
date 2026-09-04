/*
 * Host tests for assembler directives.
 *
 * The expected bytes were taken by running ez80asm on the same source. Several
 * of these encode a behaviour that is not the obvious one and is worth having
 * pinned:
 *
 *   - ds reserves space without writing it, blkb writes it out. A gap left by
 *     ds, align or org materialises only when a later byte lands past it, and
 *     a gap at the end of the file is not written at all.
 *   - ds refuses an initialiser; blkb takes one.
 *   - ascii and byte are spellings of db, so they take a list.
 *   - align takes a power of two and aligns the address, not the buffer.
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
    static char out[2048];
    char path[] = "/tmp/zap_dir_XXXXXX";
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

static void dir_is(const char* name, const char* src, const char* want) {
    const char* got = emit(src);
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-38s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-38s got %s, want %s\n", name, got, want);
        failures++;
    }
}

int main(void) {
    /* Data, little-endian at the width the directive names. */
    dir_is("db",   "  db 1,2,3\n",          "01 02 03");
    dir_is("dw",   "  dw 1234h\n",          "34 12");
    dir_is("defw", "  defw 1234h\n",        "34 12");
    dir_is("dw24", "  dw24 123456h\n",      "56 34 12");
    dir_is("dl",   "  dl 123456h\n",        "56 34 12");
    dir_is("dw32", "  dw32 12345678h\n",    "78 56 34 12");

    /* ascii and byte are db; asciz is db plus a terminator. All take a
     * list, not just one string. */
    dir_is("ascii with a list", "  ascii \"Hi\",0\n",  "48 69 00");
    dir_is("byte with a list",  "  byte \"Hi\",0\n",   "48 69 00");
    dir_is("asciz",             "  asciz \"Hi\"\n",    "48 69 00");
    dir_is("db with a string",  "  db \"Hi\",0\n",     "48 69 00");

    /* A string is read character by character, not through the token stream.
     * An apostrophe in one used to start a character literal, so db "\'"
     * could not be written. */
    dir_is("apostrophe in a string", "  db \"a'b\"\n", "61 27 62");
    dir_is("escapes in a string",
           "  db \"\\a\\b\\e\\f\\n\\r\\t\\v\\\\\\'\\\"\\?\"\n",
           "07 08 1B 0C 0A 0D 09 0B 5C 27 22 3F");

    /* ds reserves, blkb writes. The difference shows at the end of a file:
     * ds leaves nothing behind, blkb leaves its bytes. */
    dir_is("ds at end of file",   "  db 1\n  ds 3\n",         "01");
    dir_is("ds then a byte",      "  db 1\n  ds 3\n  db 2\n", "01 FF FF FF 02");
    dir_is("blkb at end of file", "  blkb 4\n",               "FF FF FF FF");
    dir_is("blkb with a value",   "  blkb 4,0AAh\n",          "AA AA AA AA");
    dir_is("blkw",                "  blkw 2,1234h\n",         "34 12 34 12");
    dir_is("blkp",                "  blkp 2,123456h\n",       "56 34 12 56 34 12");
    dir_is("blkl",                "  blkl 1,12345678h\n",     "78 56 34 12");
    /* An initialiser is read and discarded rather than refused: the reference
     * warns and reserves the space uninitialised, so ".ds 4,0" zeroes nothing
     * in either assembler. Matching it matters more than being stricter. */
    dir_is("ds ignores an initializer", "  ds 4,0\n  db 9\n", "FF FF FF FF 09");

    /* fillbyte changes what a gap is filled with. */
    dir_is("fillbyte", "  fillbyte 0EEh\n  blkb 4\n", "EE EE EE EE");
    dir_is("fillbyte fills a gap",
           "  fillbyte 0EEh\n  db 1\n  ds 2\n  db 2\n", "01 EE EE 02");

    /* align moves the address to a boundary. It used to pad up to the
     * absolute buffer offset the operand named, so ".align 64" after more
     * than 64 bytes did nothing. */
    dir_is("align pads to a boundary", "  db 1\n  align 4\n  db 2\n", "01 FF FF FF 02");
    dir_is("align when already there", "  align 4\n  db 2\n",         "02");
    dir_is("align at end of file",     "  db 1\n  align 4\n",         "01");
    dir_is("align must be a power of 2", "  db 1\n  align 15\n  db 2\n", "ERR");

    /* org moves the address; the gap fills when something lands past it, and
     * moving backwards is refused. */
    dir_is("org leaves a gap",
           "  org 40000h\n  db 1\n  org 40008h\n  db 2\n",
           "01 FF FF FF FF FF FF FF 02");
    dir_is("org backwards", "  org 40000h\n  db 1\n  org 40000h\n  db 2\n", "ERR");

    /* A list needs at least one element, and a comma needs one after it. */
    dir_is("db with no argument",   "  db\n",     "ERR");
    dir_is("db with a trailing comma", "  db 0,\n", "ERR");

    /* relocate reports the addresses the code will run at while the bytes
     * keep landing where they are written. */
    dir_is("relocate moves reported addresses",
           "  org 40000h\n  relocate 50000h\nhere:\n  dw24 here\n  dw24 $\n  endrelocate\n",
           /* here is 0x50000; the $ on the next line is 0x50003, three bytes
            * further on. Checked against ez80asm. */
           "00 00 05 03 00 05");
    /* An unclosed relocate at end of file is not an error there, so it is not
     * one here either. Nesting is. */
    dir_is("relocate cannot nest",
           "  relocate 50000h\n  relocate 60000h\n  endrelocate\n  endrelocate\n", "ERR");
    dir_is("endrelocate needs relocate", "  endrelocate\n", "ERR");
    dir_is("relocate address range", "  relocate -1\n  endrelocate\n", "ERR");

    /* A constant defined in an included file has to be visible to the same
     * forward uses as one defined here. The prescan skipped includes, so
     * "rst target" worked when target's equ was in this file and failed when
     * it was one line further into an include -- and ez80asm assembles both. */
    {
        char inc[] = "/tmp/zap_inc_XXXXXX";
        int fd = mkstemp(inc);
        if (fd >= 0) {
            const char* body = "target: equ 8\n";
            ssize_t w = write(fd, body, strlen(body));
            close(fd);

            char src[512];
            snprintf(src, sizeof(src), "  rst target\n  .include \"%s\"\n", inc);
            const char* got = (w > 0) ? emit(src) : "<write failed>";
            if (strcmp(got, "CF") == 0) {
                fprintf(stderr, "PASS  %-38s %s\n", "constant from an include", got);
            } else {
                fprintf(stderr, "FAIL  %-38s got %s, want CF\n",
                        "constant from an include", got);
                failures++;
            }
            unlink(inc);
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
