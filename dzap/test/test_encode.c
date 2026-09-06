/*
 * Host tests for dzap's instruction encoding.
 *
 * Expected bytes come from the vendored ez80asm (test/ref), generated rather
 * than transcribed. dzap exists to be measured against that assembler, so a
 * byte it disagrees on is a bug in dzap whatever the manual says.
 *
 * What is worth pinning as a unit test, rather than leaving to the reference
 * comparison in run.sh, is the cases where the encoding depends on something a
 * refactor can quietly break:
 *
 *   - row selection, which walks the candidate rows for a mnemonic testing
 *     operand modes and register sets. `ld` has 57 rows and `ld (ix+8), a`
 *     reaches the forty-third of them, so it fails first when the walk is
 *     wrong. `nop` matches a row whose register sets are both empty, and
 *     `ret nz` matches through the condition-code flag rather than the mode,
 *     which are the two special cases in the test;
 *   - the DD/FD prefixes and the signed displacement byte;
 *   - immediate width, which is one, two or three bytes and is the thing that
 *     goes wrong when the emitter's byte extraction is rewritten.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* dzap is one translation unit and everything in it is static, so there is no
 * library to link against: the test includes the program and moves its main
 * out of the way. */
#define main dzap_main
#include "dzap.c"
#undef main

static int failures = 0;

/* Assembles one source and returns its bytes as "3E 42", or "ERR". */
static const char* emit(const char* src) {
    static char out[1024];
    char path[] = "/tmp/dzap_enc_XXXXXX";
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

    build_tables();
    build_cclass();

    dz z;
    memset(&z, 0, sizeof(z));

    const bool ok = run(&z, path);
    unlink(path);

    int n = 0;
    out[0] = 0;
    if (!ok) {
        n = snprintf(out, sizeof(out), "ERR");
    } else {
        for (int i = 0; i < z.pos && n < (int) sizeof(out) - 4; i++) {
            n += snprintf(&out[n], sizeof(out) - (size_t) n, "%s%02X",
                          i ? " " : "", z.out[i]);
        }
    }
    out[n] = 0;

    free(z.out);
    br_destroy(&z.rd);

    return out;
}

static void check(const char* what, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-24s %s\n", what, got);
    } else {
        fprintf(stderr, "FAIL  %-24s got %s, want %s\n", what, got, want);
        failures++;
    }
}

/* One instruction on a line of its own, which is how the generator produced
 * the expected bytes. */
static void check_insn(const char* insn, const char* want) {
    char src[128];
    snprintf(src, sizeof(src), "  %s\n", insn);
    check(insn, emit(src), want);
}

int main(void) {
    static const struct { const char* insn; const char* bytes; } cases[] = {
        /* No operands at all: both register sets on the matching row are
         * empty, which is its own branch in row selection. */
        { "nop", "00" },
        { "halt", "76" },
        { "ccf", "3F" },
        { "ldir", "ED B0" },
        { "neg", "ED 44" },

        /* Registers, including the (hl) indirection that is a mode rather
         * than a register. */
        { "ld a, b", "78" },
        { "ld b, (hl)", "46" },
        { "ld (hl), c", "71" },
        { "add a, b", "80" },
        { "or (hl)", "B6" },
        { "ex de, hl", "EB" },
        { "add hl, de", "19" },
        { "sbc hl, bc", "ED 42" },
        { "mlt de", "ED 5C" },
        { "ld a, i", "ED 57" },
        { "ld sp, hl", "F9" },
        { "inc hl", "23" },
        { "dec sp", "3B" },
        { "push af", "F5" },
        { "pop iy", "FD E1" },

        /* Immediate widths: one byte, and the three-byte form ADL mode gives
         * a 16-bit literal. */
        { "ld a, 0x42", "3E 42" },
        { "ld a, 66", "3E 42" },
        { "cp 0x7F", "FE 7F" },
        { "and 0xAA", "E6 AA" },
        { "xor 0x0F", "EE 0F" },
        { "ld bc, 0x1234", "01 34 12 00" },
        { "ld hl, 0x123456", "21 56 34 12" },
        { "ld a, (0x040100)", "3A 00 01 04" },
        { "ld hl, (0x040200)", "2A 00 02 04" },

        /* Index displacement: the DD/FD prefix and a signed byte. The last
         * one is the forty-third row of ld. */
        { "ld a, (ix+0)", "DD 7E 00" },
        { "ld a, (ix-1)", "DD 7E FF" },
        { "ld (iy-128), b", "FD 70 80" },
        { "adc a, (ix+0)", "DD 8E 00" },
        { "ld (ix+8), a", "DD 77 08" },

        /* Bit operations, where the operand index is folded into the opcode
         * by a shift. */
        { "bit 3, (iy+4)", "FD CB 04 5E" },
        { "res 7, b", "CB B8" },
        { "set 0, (hl)", "CB C6" },
        { "rlc d", "CB 02" },
        { "srl c", "CB 39" },

        /* Condition codes, which match a row through its own flag rather than
         * through the operand mode. */
        { "ret nz", "C0" },
        { "ret z", "C8" },
        { "jp z, 0x040000", "CA 00 00 04" },
        { "jp 0x040000", "C3 00 00 04" },
        { "call nc, 0x040000", "D4 00 00 04" },

        /* Literals, at every digit count and both cases. The hex path
         * assembles the value a byte at a time from the last digit back, so
         * an odd number of digits and a value that does not fill three bytes
         * are the two cases that go wrong. */
        { "ld a, 0x0", "3E 00" },
        { "ld a, 0x7", "3E 07" },
        { "ld a, 0xff", "3E FF" },
        { "ld a, 0xFF", "3E FF" },
        { "ld a, 0xAb", "3E AB" },
        { "ld hl, 0x1", "21 01 00 00" },
        { "ld hl, 0x12", "21 12 00 00" },
        { "ld hl, 0x123", "21 23 01 00" },
        { "ld hl, 0x1234", "21 34 12 00" },
        { "ld hl, 0x12345", "21 45 23 01" },
        { "ld hl, 0x123456", "21 56 34 12" },
        { "ld hl, 0xabcdef", "21 EF CD AB" },
        { "ld hl, 0xABCDEF", "21 EF CD AB" },
        { "ld hl, 0xfedcba", "21 BA DC FE" },
        { "ld bc, 0x000001", "01 01 00 00" },
        { "ld de, 0xff00ff", "11 FF 00 FF" },
        { "ld a, 0", "3E 00" },
        { "ld a, 9", "3E 09" },
        { "ld a, 10", "3E 0A" },
        { "ld a, 99", "3E 63" },
        { "ld a, 255", "3E FF" },
        { "ld hl, 65535", "21 FF FF 00" },
        { "ld hl, 1000000", "21 40 42 0F" },
        { "ld a, -1", "3E FF" },

        { "im 2", "ED 5E" },
        { "rst 0x18", "DF" },
        { "out (0xFE), a", "D3 FE" },
        { "in a, (0xFE)", "DB FE" },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        check_insn(cases[i].insn, cases[i].bytes);
    }

    /* Forms with no matching row. The reference rejects every one of these,
     * so accepting one would be a disagreement even though it emits bytes
     * that look plausible. */
    static const char* bad[] = {
        "ld (hl), (hl)",
        "add hl, a",
        "ex hl, hl",
        "ld i, b",
        "frobnicate",
        "ld a, b, c",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        check_insn(bad[i], "ERR");
    }

    /* Case and spacing are not part of the encoding. */
    check("upper case", emit("  LD A, B\n"), "78");
    check("mixed case", emit("  Ld A, (Ix+8)\n"), "DD 7E 08");
    check("no space after comma", emit("  ld a,b\n"), "78");
    check("extra spaces", emit("   ld    a  ,   b   \n"), "78");
    check("tab separated", emit("\tld\ta,\tb\n"), "78");

    /* Several lines, so the output accumulates in order. */
    check("three lines", emit("  nop\n  ld a, 0x42\n  ret\n"), "00 3E 42 C9");

    /* A file whose last line has no newline still assembles: the reader has
     * to supply the end the source does not. */
    check("no trailing newline", emit("  nop\n  ld a, b"), "00 78");
    check("empty source", emit(""), "");
    check("blank lines only", emit("\n\n   \n"), "");

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
