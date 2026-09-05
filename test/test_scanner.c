/*
 * Host tests for the reader's whole-line buffer.
 *
 * The lexer points its tokens straight into the reader's buffer, which is safe
 * only because a line is never split across a refill: a read is trimmed back
 * to the last newline in it, and the partial line after that is carried to the
 * front of the buffer next time. Nothing else in the lexer has to know about
 * buffer boundaries.
 *
 * So the cases worth pinning are the ones that exercise the carry, and they
 * only occur at a 16 KiB boundary -- no ordinary source reaches one, which is
 * exactly why they need a test.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lexer.h"
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

/* Assembles from a file: the buffer boundary only exists for one. */
static bool asm_file(const char* text, int len, zap_result* r) {
    char path[] = "/tmp/zap_scan_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return false;
    }
    if (write(fd, text, (size_t) len) != len) {
        close(fd);
        unlink(path);

        return false;
    }
    close(fd);
    const bool ok = zap_assemble_file(path, r);
    unlink(path);

    return ok;
}

/* Pads with "nop" lines until the marker line starts at exactly `at`, then
 * emits "db 0AAh" -- the only 0xAA the output can contain. */
static int build_at(char* buf, int max, int at) {
    int n = 0;
    while (n + 4 <= at && n + 4 < max) {
        memcpy(&buf[n], "nop\n", 4);
        n += 4;
    }
    while (n < at && n + 1 < max) {
        const char c = (at - n == 1) ? '\n' : ' ';
        buf[n] = c;
        n++;
    }
    n += snprintf(&buf[n], (size_t) (max - n), " db 0AAh\n");

    return n;
}

int main(void) {
    const int BLOCK = LEX_BUF_KB * 1024;

    /* A line starting just before, on, and just after the boundary. The ones
     * before it straddle the read and have to be carried whole. */
    {
        char* buf = malloc(BLOCK * 2);
        check("scratch allocated", buf != NULL);

        const int offsets[] = {
            BLOCK - 20, BLOCK - 10, BLOCK - 9, BLOCK - 8, BLOCK - 5,
            BLOCK - 2, BLOCK - 1, BLOCK, BLOCK + 1, BLOCK + 6,
        };
        for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
            const int len = build_at(buf, BLOCK * 2, offsets[i]);
            zap_result r;
            bool ok = asm_file(buf, len, &r) && r.ok;
            int found = 0;
            for (int b = 0; ok && b < r.size; b++) {
                if (r.bytes[b] == 0xAA) {
                    found++;
                }
            }
            char name[64];
            snprintf(name, sizeof(name), "line at buffer boundary %+d",
                     offsets[i] - BLOCK);
            check(name, ok && found == 1);
            zap_free(&r);
        }
        free(buf);
    }

    /* Several buffers' worth, so the carry happens repeatedly rather than
     * once, and every statement still lands. */
    {
        const int want = 40000;
        char* buf = malloc(want * 5 + 64);
        int n = 0;
        for (int i = 0; i < want; i++) {
            memcpy(&buf[n], "nop\n", 4);
            n += 4;
        }
        zap_result r;
        const bool ok = asm_file(buf, n, &r) && r.ok;
        check("many refills, every statement kept", ok && r.size == want);
        zap_free(&r);
        free(buf);
    }

    /* A line longer than the whole buffer is the only way a read can fail to
     * end on a newline, and it is reported rather than silently truncated. */
    {
        const int len = BLOCK + 512;
        char* buf = malloc(len + 8);
        memcpy(buf, "  db ", 5);
        for (int i = 5; i < len; i++) {
            buf[i] = (i % 2) ? '1' : ',';
        }
        buf[len - 1] = '\n';
        zap_result r;
        const bool ok = asm_file(buf, len, &r) && r.ok;
        check("a line longer than the buffer is refused", !ok);
        if (r.ndiags == 1) {
            check("  reported as too long",
                  strstr(r.diags[0].msg, "too long") != NULL);
        }
        zap_free(&r);
        free(buf);
    }

    /* A short read is the end of the file, not a line that failed to end. A
     * last line with no newline is still a line. */
    {
        zap_result r;
        const char* tail = "  nop\n  ret";
        const bool ok = asm_file(tail, (int) strlen(tail), &r) && r.ok;
        check("no trailing newline", ok && r.size == 2 && r.bytes[1] == 0xC9);
        zap_free(&r);
    }

    /* CRLF: the carriage return is whitespace and must not change the bytes. */
    {
        zap_result r;
        const char* crlf = "  nop\r\n  ret\r\n";
        const bool ok = asm_file(crlf, (int) strlen(crlf), &r) && r.ok;
        check("CRLF line endings", ok && r.size == 2
              && r.bytes[0] == 0x00 && r.bytes[1] == 0xC9);
        zap_free(&r);
    }

    /* Empty, and a single newline. */
    {
        zap_result r;
        check("empty file", asm_file("", 0, &r) && r.size == 0);
        zap_free(&r);
        check("one empty line", asm_file("\n", 1, &r) && r.size == 0);
        zap_free(&r);
    }

    /* A macro body crossing a buffer boundary. lex_capture reads whole lines
     * rather than tokens, and it used to refill raw -- which does not trim
     * back to a newline, so it left the buffer ending mid-line. The scanner
     * does not test for a refill on every character, so the next token was
     * truncated there. Seven placements in a sweep of forty-one either failed
     * or, worse, assembled to different bytes.
     *
     * The macro is placed at every offset across the boundary, since only a
     * few alignments trip it. */
    {
        char* buf = malloc(BLOCK * 2);
        int bad = 0;
        for (int pad = BLOCK - 96; pad < BLOCK + 48; pad++) {
            int n = snprintf(buf, 64, "  .assume adl=1\n  .org 0\n");
            while (n + 6 <= pad) {
                memcpy(&buf[n], "  nop\n", 6);
                n += 6;
            }
            while (n < pad) {
                buf[n++] = ' ';
            }
            buf[n++] = '\n';
            n += snprintf(&buf[n], 80,
                          "  macro m\n  ld a,b\n  ld a,c\n  endmacro\n"
                          "  m\n  ld b,c\n  ret\n");

            zap_result r;
            const bool ok = asm_file(buf, n, &r) && r.ok;
            /* The expansion and the statements after it must all be there:
             * ld a,b / ld a,c / ld b,c / ret at the end of the output. */
            const bool tail = ok && r.size >= 4
                           && r.bytes[r.size - 4] == 0x78
                           && r.bytes[r.size - 3] == 0x79
                           && r.bytes[r.size - 2] == 0x41
                           && r.bytes[r.size - 1] == 0xC9;
            if (!tail) {
                if (bad == 0) {
                    fprintf(stderr, "      first bad placement at pad %d\n", pad);
                }
                bad++;
            }
            zap_free(&r);
        }
        check("macro body across a buffer boundary, every placement", bad == 0);
        free(buf);
    }

    /* Token text points into the buffer, so a statement whose operands are
     * still needed after later tokens have been read has to survive. A
     * deferred forward reference is the case that reads text back late. */
    {
        char* buf = malloc(BLOCK * 2);
        int n = snprintf(buf, 64, "  .assume adl=1\n  .org 0\n  dl later\n");
        while (n < BLOCK + 32) {
            n += snprintf(&buf[n], 8, "  nop\n");
        }
        n += snprintf(&buf[n], 32, "later:\n  ret\n");
        zap_result r;
        const bool ok = asm_file(buf, n, &r) && r.ok;
        /* The address is patched in after many refills have happened. */
        const int addr = ok && r.size > 3
                       ? r.bytes[0] | (r.bytes[1] << 8) | (r.bytes[2] << 16) : -1;
        check("forward reference resolved across many refills",
              ok && addr == r.size - 1);
        zap_free(&r);
        free(buf);
    }

    /* The prescan's line skipper.
     *
     * It stops on the first line holding either wanted character and leaves
     * the reader positioned at the start of that line; every line before it is
     * consumed and counted. The line count matters as much as the position --
     * diagnostics from the main pass are numbered from it, so a skipper that
     * lands in the right place with the wrong count reports every later error
     * against the wrong line. It used to reach the newline twice, once to find
     * it and once to step over it; folding those together is only correct if
     * both still come out exactly here. */
    {
        static const char* SRC =
            "  nop\n"          /* 1 */
            "  ret\n"          /* 2 */
            "lab: equ 4\n"     /* 3 -- has both */
            "  nop\n";         /* 4 -- lcount_ starts at 1, so this lands on 3 */
        char path[] = "/tmp/zap_skip_XXXXXX";
        int fd = mkstemp(path);
        check("skip: scratch file", fd >= 0);
        const size_t n = strlen(SRC);
        check("skip: written", write(fd, SRC, n) == (long) n);
        close(fd);

        lexer lex;
        check("skip: opened", lex_init(&lex, path) != NULL);
        lex_skip_lines_without(&lex, ':', '=');
        check("skip: consumed exactly two lines", lex.lcount_ == 3);

        /* Positioned at the start of that line, not past it. */
        token tk;
        lex_next(&lex, &tk);
        check("skip: left the reader at the label",
              tk.sz_ == 3 && memcmp(tk.txt_, "lab", 3) == 0);
        lex_destroy(&lex);
        unlink(path);
    }

    /* Nothing matches: every line is consumed and counted, and a last line
     * without a newline terminates rather than spinning. */
    {
        static const char* SRC = "  nop\n  ret\n  nop";
        char path[] = "/tmp/zap_skip_XXXXXX";
        int fd = mkstemp(path);
        check("skip2: scratch file", fd >= 0);
        const size_t n = strlen(SRC);
        check("skip2: written", write(fd, SRC, n) == (long) n);
        close(fd);

        lexer lex;
        check("skip2: opened", lex_init(&lex, path) != NULL);
        lex_skip_lines_without(&lex, ':', '=');
        check("skip2: counted both newlines", lex.lcount_ == 3);

        token tk;
        lex_next(&lex, &tk);
        check("skip2: ran out of source", tk.tk_ == NONE);
        lex_destroy(&lex);
        unlink(path);
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
