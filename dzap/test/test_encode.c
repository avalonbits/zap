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
        const int len = (int) (z.o - z.out);
        for (int i = 0; i < len && n < (int) sizeof(out) - 4; i++) {
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

        /* More digits than fit. The value keeps the low three bytes and the
         * rest are dropped, which is what the reference does. These matter
         * because the digits past the sixth are never assembled, so they are
         * only seen by the check that runs after it. */
        { "ld hl, 0x1234567", "21 67 45 23" },
        { "ld hl, 0x12345678", "21 78 56 34" },

        /* Relative jumps. The displacement is measured from the instruction
         * after this one, so it depends on where the instruction sits -- each
         * of these is the first line of its own source, at 0x040000. These
         * assembled to the target address truncated to a byte until the
         * transform was implemented. The last two are the limits, +127 and
         * -126 from the following instruction. */
        { "jr 0x040000", "18 FE" },
        { "jr nz, 0x040000", "20 FE" },
        { "jr z, 0x040000", "28 FE" },
        { "jr nc, 0x040000", "30 FE" },
        { "jr c, 0x040000", "38 FE" },
        { "djnz 0x040000", "10 FE" },
        { "jr 0x04007F", "18 7D" },
        { "jr 0x03FF82", "18 80" },

        /* A hexadecimal literal written with a trailing h begins with a
         * letter, so the operand parser saw a name and said "unknown
         * operand". num_parse had always understood the suffix; nothing ever
         * reached it. Forty forms of the reference's own corpus were wrong for
         * as long as that was true. */
        { "ld hl, aabbcch", "21 CC BB AA" },
        { "ld a, (aabbh)", "3A BB AA 00" },
        { "ld a, 0ffh", "3E FF" },

        /* lea and pea take a displacement on a bare register: their rows ask
         * for NOREQ with F_DISPA or F_DISPB, not INDIRECT, and the parser
         * looked for a displacement only inside parentheses. */
        { "lea bc, ix+5", "ED 02 05" },
        { "lea iy, ix+5", "ED 55 05" },
        { "pea ix+5", "ED 65 05" },
        { "pea iy-3", "ED 66 FD" },

        /* The shadow accumulator, which the table holds as plain R_AF -- the
         * row is R_AF on both sides, so the apostrophe only has to be
         * accepted. */
        { "ex af, af'", "08" },

        /* Negative literals, which reach the emitter through a branch that
         * negates the value. The reference corpus has exactly one negative
         * displacement and no negative immediate at all. */
        { "ld hl, -1", "21 FF FF FF" },
        { "ld a, -128", "3E 80" },
        { "ld a, (ix-128)", "DD 7E 80" },

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

    /* A digit that is not hex, in each position that is handled differently:
     * the only digit, the low half of a byte, the last digit assembled, and
     * one past the sixth. That last is the interesting one: counting back from
     * the end, the assembly consumes six digits, so a bad digit before those
     * is never looked at there and is caught only by the loop that follows.
     * The reference refuses all four. */
    check("bad digit alone", emit("  ld a, 0xZ\n"), "ERR");
    check("bad digit low half", emit("  ld a, 0x4Z\n"), "ERR");
    check("bad digit high half", emit("  ld a, 0xZ4\n"), "ERR");
    check("bad digit high half of byte 2", emit("  ld hl, 0x12Z456\n"), "ERR");
    check("bad digit last assembled", emit("  ld hl, 0x12345Z\n"), "ERR");
    check("bad digit past the sixth", emit("  ld hl, 0xZ1234567\n"), "ERR");

    /* Case and spacing are not part of the encoding. */
    check("upper case", emit("  LD A, B\n"), "78");
    check("mixed case", emit("  Ld A, (Ix+8)\n"), "DD 7E 08");
    check("no space after comma", emit("  ld a,b\n"), "78");
    check("extra spaces", emit("   ld    a  ,   b   \n"), "78");
    check("tab separated", emit("\tld\ta,\tb\n"), "78");

    /* A relative jump measured from a moving address. Assembling each on its
     * own cannot catch a displacement that ignores how far into the output it
     * is, because there every instruction sits at the origin. */
    check("relative jumps down a sequence",
          emit("  jr 0x040000\n  jr nz, 0x040000\n  djnz 0x040000\n"
               "  nop\n  jr 0x040020\n  jr c, 0x040000\n  jr 0x03FF90\n"),
          "18 FE 20 FC 10 FA 00 18 17 38 F5 18 83");

    /* Out of reach in both directions. The reference refuses these and so
     * must dzap, rather than emitting a wrapped byte. */
    check("too far forward", emit("  jr 0x040082\n"), "ERR");
    check("too far back", emit("  jr 0x03FF7F\n"), "ERR");

    /* Several lines, so the output accumulates in order. */
    check("three lines", emit("  nop\n  ld a, 0x42\n  ret\n"), "00 3E 42 C9");

    /* A file whose last line has no newline still assembles: the reader has
     * to supply the end the source does not. */
    check("no trailing newline", emit("  nop\n  ld a, b"), "00 78");
    check("empty source", emit(""), "");
    check("blank lines only", emit("\n\n   \n"), "");

    /* A token too long to be a mnemonic must be rejected before it indexes
     * the bucket table.
     *
     * The bucket is `letter_base[first] + length` and letter_base steps by
     * NLEN, so a length of NLEN or more runs off the end of its own letter.
     * Landing on another letter's bucket is harmless -- the first character
     * cannot match, so same_ci rejects it -- but a mnemonic character that is
     * not a letter has base 208, the last of the 27, and 208 + 8 is past the
     * end of a 216-entry table. `_` and the digits are mnemonic characters,
     * so the source can ask for that read.
     *
     * Both are here: the first only documents the aliasing, the second is the
     * one that bites, under the sanitiser the host runner turns on. */
    check("a long token lands on another letter's bucket harmlessly",
          emit("  mmmmmmmmmmm\n"), "ERR");
    check("a long token cannot index past the bucket table",
          emit("  _________\n"), "ERR");

    /* The hexadecimal fast path, driven directly.
     *
     * It has a correct fallback: hex_digits rejects anything that is not a
     * digit run, and num_parse then produces the right answer more slowly. So
     * disabling the fast path entirely, or handing it the wrong slice of the
     * token, changes no output and no check above can see it -- verified, all
     * three fail zero. What the encodings do catch is a fast path that
     * produces a *wrong* value: swapping two of the bytes fails 40 of them.
     *
     * These call it directly so that the path being taken at all is asserted
     * somewhere, rather than left to the benchmark to notice.
     *
     * `0x1234` and `1234h` reach it as the same digit run, which is the point
     * of it taking a run rather than a token. */
    {
        int v = 0;
        char got[80];
        const bool a = hex_digits("42", 2, &v);
        const int v42 = v;
        const bool b = hex_digits("123456", 6, &v);
        const int v123456 = v;
        const bool c = hex_digits("aabbcc", 6, &v);
        const int vaabbcc = v;
        v = 0x5A5A5A;
        const bool d = hex_digits("12z4", 4, &v);   /* not hex: rejected */
        snprintf(got, sizeof(got), "%d %06X %d %06X %d %06X %d %06X",
                 a, v42, b, v123456, c, vaabbcc, d, v);
        check("hex_digits assembles a run and rejects a bad one", got,
              "1 000042 1 123456 1 AABBCC 0 5A5A5A");

        /* Seven digits: the value keeps the low three bytes and the rest are
         * dropped, but a bad digit among them still has to be rejected. */
        const bool e = hex_digits("1234567", 7, &v);
        const int v7 = v;
        const bool f = hex_digits("z234567", 7, &v);
        snprintf(got, sizeof(got), "%d %06X %d", e, v7, f);
        check("hex_digits drops digits past three bytes but still checks them",
              got, "1 234567 0");
    }

    /* Growing the output buffer.
     *
     * realloc is allowed to move the block, so out_grow has to carry the
     * cursor and the limit across with it. Nothing else here reaches that
     * code: the buffer starts at 16 KB or a quarter of the source, whichever
     * is larger, and the biggest case file emits 8,771 bytes. A rebase that
     * was simply forgotten would lose every byte written so far and no test
     * above would notice.
     *
     * Driven directly rather than through a source large enough to force it,
     * which would be a 50 KB case file to exercise four lines. */
    {
        dz z;
        memset(&z, 0, sizeof(z));
        z.cap = OUT_MIN;
        z.out = (uint8_t*) malloc((size_t) z.cap);
        z.o = z.out;
        z.lim = z.out + z.cap - OUT_MAX_INSN;
        for (int i = 0; i < 100; i++) {
            *z.o++ = (uint8_t) i;
        }

        const bool grew = out_grow(&z);
        char got[64];
        snprintf(got, sizeof(got), "%d %d %d %d", grew ? 1 : 0,
                 (int) (z.o - z.out), (int) (z.lim - z.out),
                 z.out[99] == 99 && z.out[0] == 0);
        char want[64];
        snprintf(want, sizeof(want), "1 100 %d 1", OUT_MIN + OUT_STEP - OUT_MAX_INSN);
        check("out_grow carries the cursor and the limit", got, want);

        /* And that the rebased limit still leaves room for a whole
         * instruction, which is the property the reserve relies on. */
        check("a grown buffer has room for the longest form",
              (z.lim + OUT_MAX_INSN == z.out + z.cap) ? "yes" : "no", "yes");
        free(z.out);
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
