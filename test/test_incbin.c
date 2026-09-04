/*
 * Host tests for .incbin.
 *
 * The copy runs a block at a time through br_block and pr_wblock rather than a
 * byte at a time, so the cases that matter are the ones a block copy can get
 * wrong and a byte loop cannot: a file longer than the reader's buffer, which
 * spans several blocks, and a file whose length is an exact multiple of it.
 * Rokky's images are larger than the buffer, so this is the ordinary case, not
 * a corner one.
 *
 * The byte values matter too. br_char cannot read binary -- it returns a char,
 * so a data byte of 0xFF is its EOF sentinel -- which once stopped an .incbin
 * at the first 0xFF in the file.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* Writes `n` bytes of a repeating pattern that covers every value including
 * 0x00 and 0xFF, and returns the path in `path`. */
static bool spill_bin(const char* path, int n) {
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        const unsigned char b = (unsigned char) (i % 256);
        if (fwrite(&b, 1, 1, f) != 1) {
            fclose(f);

            return false;
        }
    }

    return fclose(f) == 0;
}

/* Assembles `src` and checks the output is exactly the `n`-byte pattern,
 * offset by `skip` bytes of preceding output. */
static void copies(const char* name, const char* src, int skip, int n) {
    zap_result r;
    if (!zap_assemble_mem(src, (int) strlen(src), "t", &r) || !r.ok) {
        check(name, false);
        zap_free(&r);

        return;
    }
    bool ok = r.size == skip + n;
    for (int i = 0; ok && i < n; i++) {
        if (r.bytes[skip + i] != (unsigned char) (i % 256)) {
            fprintf(stderr, "      byte %d is %02X, want %02X\n",
                    i, r.bytes[skip + i], (unsigned) (i % 256));
            ok = false;
        }
    }
    if (r.size != skip + n) {
        fprintf(stderr, "      size %d, want %d\n", r.size, skip + n);
    }
    check(name, ok);
    zap_free(&r);
}

int main(void) {
    /* The reader opens a binary with a 4 KiB buffer, so these straddle it:
     * under it, exactly it, one past it, and several times it. A block copy
     * that stops after the first block, or loses the seam between two, shows
     * up here and nowhere else. */
    const struct { const char* name; int size; } sizes[] = {
        {"empty file",              0},
        {"one byte",                1},
        {"under one buffer",     1000},
        {"one byte under",       4095},
        {"exactly one buffer",   4096},
        {"one byte over",        4097},
        {"several buffers",     14000},
    };

    char path[64];
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        snprintf(path, sizeof(path), "/tmp/zap_incbin_%u.bin", i);
        if (!spill_bin(path, sizes[i].size)) {
            check(sizes[i].name, false);
            continue;
        }

        char src[128];
        snprintf(src, sizeof(src), "  incbin \"%s\"\n", path);
        copies(sizes[i].name, src, 0, sizes[i].size);
        unlink(path);
    }

    /* 0xFF is the sentinel br_char returns for end of file, so a copy that
     * reads chars rather than bytes stops there. The pattern above covers it,
     * but a file that opens with one pins it on its own. */
    {
        strcpy(path, "/tmp/zap_incbin_ff.bin");
        FILE* f = fopen(path, "wb");
        const unsigned char bytes[] = {0xFF, 0x00, 0xFF, 0x41, 0x00};
        const bool wrote = f != NULL && fwrite(bytes, 1, 5, f) == 5;
        if (f != NULL) {
            fclose(f);
        }

        char src[128];
        snprintf(src, sizeof(src), "  incbin \"%s\"\n", path);
        zap_result r;
        const bool ok = wrote
                     && zap_assemble_mem(src, (int) strlen(src), "t", &r)
                     && r.ok && r.size == 5
                     && memcmp(r.bytes, bytes, 5) == 0;
        check("0xFF and 0x00 survive the copy", ok);
        zap_free(&r);
        unlink(path);
    }

    /* Data before and after the copy has to land where it belongs: the block
     * write advances the address by the whole run, and a run that got that
     * wrong would still produce the right bytes in the wrong place. */
    {
        strcpy(path, "/tmp/zap_incbin_mid.bin");
        check("scratch binary written", spill_bin(path, 5000));

        char src[192];
        snprintf(src, sizeof(src),
                 "  db 1,2,3\n  incbin \"%s\"\n  db 4,5,6\n", path);
        zap_result r;
        bool ok = zap_assemble_mem(src, (int) strlen(src), "t", &r) && r.ok;
        ok = ok && r.size == 3 + 5000 + 3;
        ok = ok && r.bytes[0] == 1 && r.bytes[2] == 3;
        for (int i = 0; ok && i < 5000; i++) {
            ok = r.bytes[3 + i] == (unsigned char) (i % 256);
        }
        ok = ok && r.bytes[5003] == 4 && r.bytes[5005] == 6;
        check("data before and after a multi-block copy", ok);
        zap_free(&r);
        unlink(path);
    }

    /* A copy that starts past the high-water mark fills the gap first, and
     * that fill happens once for the run rather than once per byte. */
    {
        strcpy(path, "/tmp/zap_incbin_gap.bin");
        check("gap scratch written", spill_bin(path, 6000));

        char src[192];
        snprintf(src, sizeof(src),
                 "  .org 0\n  db 1\n  .org 4\n  incbin \"%s\"\n", path);
        zap_result r;
        bool ok = zap_assemble_mem(src, (int) strlen(src), "t", &r) && r.ok;
        ok = ok && r.size == 4 + 6000;
        ok = ok && r.bytes[0] == 1;
        for (int i = 1; ok && i < 4; i++) {
            ok = r.bytes[i] == 0xFF;   /* the default fill byte */
        }
        for (int i = 0; ok && i < 6000; i++) {
            ok = r.bytes[4 + i] == (unsigned char) (i % 256);
        }
        check("gap before a copy is filled once", ok);
        zap_free(&r);
        unlink(path);
    }

    /* A missing file is an error, not a silent empty copy. */
    {
        zap_result r;
        const char* src = "  incbin \"/tmp/zap_no_such_file_here.bin\"\n";
        check("missing binary is an error",
              !zap_assemble_mem(src, (int) strlen(src), "t", &r) || !r.ok);
        zap_free(&r);
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
