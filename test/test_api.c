/*
 * Host tests for the public interface.
 *
 * The reason this interface exists is assembling from memory: an editor
 * already holds the source, and writing it to a temporary file to assemble it
 * is both slow on an Agon and wrong when the buffer has unsaved changes. So
 * the test that matters most is that a file and the same text in memory
 * produce the same bytes.
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

/* Writes text to a scratch file and returns its path in `path`. */
static bool spill(const char* text, char* path) {
    strcpy(path, "/tmp/zap_api_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) {
        return false;
    }
    const bool ok = write(fd, text, strlen(text)) == (long) strlen(text);
    close(fd);

    return ok;
}

static const char* hex(const zap_result* r) {
    static char out[512];
    int n = 0;
    out[0] = 0;
    for (int i = 0; i < r->size && n < (int) sizeof(out) - 4; i++) {
        n += snprintf(&out[n], sizeof(out) - n, "%s%02X", i ? " " : "", r->bytes[i]);
    }
    out[n] = 0;

    return out;
}

int main(void) {
    const char* src =
        "    .assume adl=1\n"
        "    .org $40000\n"
        "start:\n"
        "    ld hl, msg\n"
        "    call prstr\n"
        "    ret\n"
        "prstr:\n"
        "    ld a,(hl)\n"
        "    or a\n"
        "    ret z\n"
        "    rst.lil 10h\n"
        "    inc hl\n"
        "    jr prstr\n"
        "msg:\n"
        "    .asciz \"Hi\\r\\n\"\n";

    /* A file and the same text in memory have to give the same bytes. That is
     * the whole point of the memory entry point. */
    char path[64];
    check("scratch file written", spill(src, path));

    zap_result from_file;
    zap_result from_mem;
    const bool fok = zap_assemble_file(path, &from_file);
    const bool mok = zap_assemble_mem(src, (int) strlen(src), "buffer", &from_mem);

    check("file assembled", fok && from_file.ok);
    check("memory assembled", mok && from_mem.ok);

    if (from_file.ok && from_mem.ok) {
        bool same = from_file.size == from_mem.size;
        for (int i = 0; same && i < from_file.size; i++) {
            same = from_file.bytes[i] == from_mem.bytes[i];
        }
        if (!same) {
            fprintf(stderr, "      file=%s\n", hex(&from_file));
        }
        check("file and memory agree byte for byte", same);
        check("origin reported", from_file.origin == 0x40000);
        check("some bytes produced", from_file.size > 0);
    }

    zap_free(&from_file);
    zap_free(&from_mem);
    unlink(path);

    /* A failure comes back as data -- file, line and message -- so a caller
     * can put it against the right line instead of parsing a printed string.
     * The line is the one in the file the error is actually in. */
    {
        zap_result r;
        const char* bad = "  ld a,b\n  ld a,\n";
        const bool ok = zap_assemble_mem(bad, (int) strlen(bad), "buffer.asm", &r);

        check("bad source rejected", !ok && !r.ok);
        check("one diagnostic", r.ndiags == 1);
        if (r.ndiags == 1) {
            check("diagnostic names the line", r.diags[0].line == 2);
            check("diagnostic names the source", strcmp(r.diags[0].file, "buffer.asm") == 0);
            check("diagnostic has a message", r.diags[0].msg[0] != 0);
            fprintf(stderr, "      -> %s line %d: %s\n",
                    r.diags[0].file, r.diags[0].line, r.diags[0].msg);
        }
        zap_free(&r);
    }

    /* Empty and comment-only sources assemble to nothing rather than failing. */
    {
        zap_result r;
        check("empty source", zap_assemble_mem("", 0, "empty", &r) && r.size == 0);
        zap_free(&r);
        check("comment only", zap_assemble_mem("; nope\n", 7, "c", &r) && r.size == 0);
        zap_free(&r);
    }

    /* zap_free is safe on a result that failed, and twice. */
    {
        zap_result r;
        zap_assemble_mem("  ld a,\n", 8, "x", &r);
        zap_free(&r);
        zap_free(&r);
        check("free is safe on a failed result", true);
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
