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

    /* dz_free, not just the output buffer. The symbol table, the name arena,
     * the fixups and the label blocks were being left behind on every one of
     * the several hundred calls here, which is 11 MB by the end and a
     * LeakSanitizer failure that made this program's exit status useless --
     * so the runner's PASS and FAIL lines were the only signal it carried. */
    dz_free(&z);

    return out;
}

/* How many local-label blocks a source ends up holding.
 *
 * The local table is emptied at the end of every scope and its storage reused,
 * which is the whole reason it is a separate table -- so a program's locals
 * cost the widest scope rather than the sum of every scope. Nothing about that
 * shows up in the bytes, so it is checked directly. */
static int local_blocks(const char* src) {
    char path[] = "/tmp/dzap_loc_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    if (write(fd, src, strlen(src)) != (long) strlen(src)) {
        close(fd);
        unlink(path);

        return -1;
    }
    close(fd);

    build_tables();
    build_cclass();

    dz z;
    memset(&z, 0, sizeof(z));
    const bool ok = run(&z, path);
    unlink(path);

    int n = 0;
    for (const locblock* b = z.locfirst; b != NULL; b = b->next) {
        n++;
    }
    dz_free(&z);

    return ok ? n : -1;
}

static int range_bad;
static void check_range(const char* what, bool ok) {
    if (!ok && range_bad++ == 0) {
        fprintf(stderr, "FAIL  %s\n", what);
        failures++;
    }
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

        /* One register from each byte plane of the register mask, on each
         * side, plus an operand holding no register at all.
         *
         * A register is one bit of a 21-bit set, and row selection holds that
         * set as three byte planes and an empty flag, on the row and on the
         * operand alike, because a 24-bit AND is a call here. Plane 0 holds
         * a..l and bc, plane 1 de..ixl, plane 2 iyh..i. Nothing in the source
         * text says which plane a register lives in -- it is decided by the
         * bit the register was given -- so a register that moves planes, or a
         * plane dropped from the test, changes which row an operand matches
         * and nothing else. `ld mb, a` becoming `ld i, a`'s opcode is what
         * that looks like, which is why these assert bytes and not
         * acceptance.
         *
         * `ld a, 5` and `jp 0x040000` are the no-register case on the B and
         * the A side. It is the one worth having twice: an operand holding no
         * register is all-zero in every plane, so it is separated from a real
         * register only by the empty flag, and a row selection that forgets
         * the flag silently accepts registers where a literal was written. */
        { "ld b, 5", "06 05" },
        { "ld hl, 0x1234", "21 34 12 00" },
        { "push af", "F5" },
        { "ld a, ixl", "DD 7D" },
        { "ld ixh, 5", "DD 26 05" },
        { "ld i, a", "ED 47" },
        { "ld a, r", "ED 5F" },
        { "ld mb, a", "ED 6D" },
        { "ld a, iyl", "FD 7D" },
        { "ld iyh, 5", "FD 26 05" },
        { "ld a, 5", "3E 05" },
        { "jp 0x040000", "C3 00 00 04" },

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

        /* Every radix syntax the reference accepts, checked against it
         * rather than taken from a manual. Hexadecimal is 0x, a trailing h,
         * $ or #; binary is 0b, a trailing b, or %; decimal is plain. Only
         * 0x and the trailing h have fast paths -- the rest reach num_parse,
         * and these are what say the two still agree. */
        { "ld a, 42h", "3E 42" },
        { "ld a, 42H", "3E 42" },
        { "ld a, $42", "3E 42" },
        { "ld a, #42", "3E 42" },
        { "ld hl, $123456", "21 56 34 12" },
        { "ld a, 1010b", "3E 0A" },
        { "ld a, 1010B", "3E 0A" },
        { "ld a, 11111111b", "3E FF" },
        { "ld a, 0b1010", "3E 0A" },
        { "ld a, %1010", "3E 0A" },
        { "ld a, 0h", "3E 00" },
        { "ld a, 0b", "3E 00" },
        { "ld a, 777", "3E 09" },

        /* A leading zero is decimal, not octal. 010 is ten and not eight,
         * 0100 is a hundred and not sixty-four, and 08 and 09 assemble --
         * which octal would refuse. Checked against ez80asm; this is the
         * assumption most likely to be imported from C by whoever adds a
         * radix next. */
        { "ld a, 010", "3E 0A" },
        { "ld a, 0100", "3E 64" },
        { "ld a, 017", "3E 11" },
        { "ld a, 08", "3E 08" },
        { "ld a, 09", "3E 09" },
        { "ld a, 00", "3E 00" },
        { "ld a, 0011", "3E 0B" },

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

        /* There is no octal, and no radix letter leads. Every one of these
         * looks like a literal somebody would expect to work and the
         * reference refuses all of them, so accepting one would be a
         * disagreement that emits plausible bytes. */
        "ld a, 777o",
        "ld a, 777q",
        "ld a, 0o777",
        "ld a, 0q777",
        "ld a, b1010",
        "ld a, o777",
        "ld a, q777",
        "ld a, h42",
        "ld a, 0h42",
        "ld a, @777",
        "ld a, &42",
        "ld a, 0d66",

        /* A digit outside the radix its suffix names. */
        "ld a, 8b",
        "ld a, 9b",
        "ld a, 1010y",
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

    /* What comes after the instruction, which the line loop decides.
     *
     * It asks for the newline first and only then looks for trailing space or
     * a remark, because the newline is the answer nearly every time. All four
     * shapes of tail go through that branch, and the reference refuses the
     * two with text in them. Removing the "unexpected text" report failed no
     * check at all before these: a trailing token errors either way, just
     * later and with a different message, so nothing pinned which. */
    check("trailing text is refused", emit("  nop x\n"), "ERR");
    check("trailing text after operands is refused",
          emit("  ld a, b junk\n"), "ERR");
    check("trailing space alone", emit("  ld a, b \t\n"), "78");
    check("trailing space then a remark", emit("  nop   \t ; remark\n"), "00");
    check("remark with no space before it", emit("  nop; remark\n"), "00");

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

    /* Labels.
     *
     * The origin is 0x040000, so a label at the top of a source is that
     * address. Every expectation here was taken from ez80asm.
     *
     * A label is recognised by the colon and not by the column: the reference
     * takes one at any indent, so position decides nothing. */
    check("a label and a backward jump", emit("foo:\n  jp foo\n"), "C3 00 00 04");
    check("a label indented", emit("  foo:\n  jp foo\n"), "C3 00 00 04");
    check("a label sharing a line with an instruction",
          emit("foo: ld a,b\n  jp foo\n"), "78 C3 00 00 04");
    check("a label as an immediate", emit("foo:\n  ld hl, foo\n"), "21 00 00 04");
    check("a label inside an indirect", emit("foo:\n  ld a, (foo)\n"), "3A 00 00 04");
    check("a backward relative jump to a label",
          emit("foo:\n  jr foo\n"), "18 FE");

    /* Forward references, which are the whole reason there is a fixup list.
     * dzap reads a line once and never returns to it, so the bytes go down as
     * zero and are patched when the source runs out. */
    check("a forward jump", emit("  jp foo\nfoo:\n  ret\n"), "C3 04 00 04 C9");
    check("a forward immediate", emit("  ld hl, foo\nfoo:\n"), "21 04 00 04");
    check("a forward relative jump", emit("  jr foo\nfoo:\n  ret\n"), "18 00 C9");
    check("a forward reference resolved twice",
          emit("  jp foo\n  jp foo\nfoo:\n"), "C3 08 00 04 C3 08 00 04");

    /* A relative jump out of reach is refused, and out of reach can only be
     * known once the label is. Built rather than written out, because it
     * takes more than 127 bytes to get there -- and `ds 200` would have made
     * this pass for the wrong reason, dzap having no directives. */
    {
        static char far[2048];
        int n = snprintf(far, sizeof(far), "  jr foo\n");
        for (int i = 0; i < 130; i++) {
            n += snprintf(&far[n], sizeof(far) - (size_t) n, "  nop\n");
        }
        snprintf(&far[n], sizeof(far) - (size_t) n, "foo:\n  ret\n");
        check("a forward relative jump too far", emit(far), "ERR");

        n = snprintf(far, sizeof(far), "  jr foo\n");
        for (int i = 0; i < 100; i++) {
            n += snprintf(&far[n], sizeof(far) - (size_t) n, "  nop\n");
        }
        snprintf(&far[n], sizeof(far) - (size_t) n, "foo:\n  ret\n");
        check("a forward relative jump just in reach",
              strncmp(emit(far), "18 64", 5) == 0 ? "18 64" : emit(far), "18 64");
    }

    /* The errors the reference gives. */
    check("a label defined twice", emit("foo:\nfoo:\n  nop\n"), "ERR");
    check("a label that is never defined", emit("  jp nowhere\n"), "ERR");
    check("labels are case sensitive", emit("FOO:\n  jp foo\n"), "ERR");

    /* A name and a number are told apart by the suffix, not by the first
     * character: both can begin with a letter. `aabbcch` is a literal even
     * though a label could be spelled that way, which is what the reference
     * does. */
    check("a trailing-h literal is not a label",
          emit("  ld hl, aabbcch\n"), "21 CC BB AA");
    check("a label that looks like hex without the suffix",
          emit("aabbcc:\n  ld hl, aabbcc\n"), "21 00 00 04");

    /* The radix prefixes are literals and not names. Asking "does it start
     * with a digit" made all three into labels that did not exist. */
    check("a dollar literal is not a label", emit("  ld a, $42\n"), "3E 42");
    check("a hash literal is not a label", emit("  ld a, #42\n"), "3E 42");
    check("a percent literal is not a label", emit("  ld a, %1010\n"), "3E 0A");

    /* Enough labels, and long enough ones, to grow every arena more than once.
     *
     * The symbols, their names and the fixups are all realloc'd in blocks, and
     * the buckets and the fixups hold pointers into them -- so a growth has to
     * rebase what points at the old block. Short names did not show it: six
     * hundred of them is 6 KB against an 8 KB step, so the arena never moved
     * and deleting the rebase failed nothing. These are 30 characters each,
     * which is past the step several times over and is also the length real
     * Agon labels reach -- the longest in the full corpus is 38. */
    {
        static char big[120000];
        static const char* pad = "_padded_out_to_thirty_chars";
        int n = 0;
        for (int i = 0; i < 600; i++) {
            n += snprintf(&big[n], sizeof(big) - (size_t) n,
                          "  jp l%s%03d\n", pad, i);
        }
        for (int i = 0; i < 600; i++) {
            n += snprintf(&big[n], sizeof(big) - (size_t) n,
                          "l%s%03d:\n  nop\n", pad, i);
        }
        const char* got = emit(big);
        /* The first jump targets the first label, which sits after 600 jumps
         * of four bytes each. */
        char want[16];
        snprintf(want, sizeof(want), "C3 %02X %02X 04", (600 * 4) & 0xFF,
                 ((600 * 4) >> 8) & 0xFF);
        check("six hundred long labels, growing every arena",
              strncmp(got, want, strlen(want)) == 0 ? want : got, want);
    }

    /* The symbol key distributes.
     *
     * Nothing above can see this. A bad key is still *correct* -- every name
     * lands in some bucket and the chain finds it -- so zeroing the Pearson
     * table, or dropping its second pass, fails no encoding check at all.
     * Both were tried. What a bad key costs is time: 699 labels in one bucket
     * took a benchmark from 1.88s to 5.98s.
     *
     * So the property to assert is the distribution, on the naming style that
     * broke the key this replaced -- one stem, numbered, which shares a first
     * character, a last character and a length. */
    {
        static char names[700][32];
        static int seen[NSYMB];
        int used = 0, worst = 0;
        for (int i = 0; i < NSYMB; i++) {
            seen[i] = 0;
        }
        for (int i = 0; i < 700; i++) {
            snprintf(names[i], sizeof(names[i]), "lbl_routine_body_%04d", i);
            const int b = sym_bucket(names[i], (int) strlen(names[i]));
            check_range("bucket in range", b >= 0 && b < NSYMB);
            if (seen[b]++ == 0) {
                used++;
            }
            if (seen[b] > worst) {
                worst = seen[b];
            }
        }
#if DZ_SYMHASH
        /* 700 names into 2,048 buckets: a good hash uses most of them and
         * chains stay short. The structural key put all 700 in one. */
        check("clustered names spread over many buckets",
              used > 400 ? "many" : "few", "many");
        check("clustered names leave chains short",
              worst <= 6 ? "short" : "long", "short");
#else
        (void) used;
        (void) worst;
#endif
    }

    /* A label cannot be spelled like a literal.
     *
     * The reference refuses `a00h:`, `ffh:`, `e5h:`, `ah:` and `1010b:` --
     * numbers with a radix suffix -- while accepting `beef:`, `zzh:`, `h:`
     * and `a0h_x:`, none of which are numbers. dzap accepted every one of
     * them, which is the direction that produces plausible bytes rather than
     * an error: `ffh:` defined a label the reference would have refused, and
     * a later `ld a, ffh` then meant different things in the two assemblers.
     *
     * Found by a benchmark generator that produced `a00h` by accident, which
     * is the only reason it was found at all -- no test reached it. */
    check("a label spelled as trailing-h hex", emit("a00h:\n  nop\n"), "ERR");
    check("a short trailing-h label", emit("ah:\n  nop\n"), "ERR");
    check("a trailing-h label with a leading zero", emit("0ffh:\n  nop\n"), "ERR");
    check("a label spelled as binary", emit("1010b:\n  nop\n"), "ERR");
    check("a label spelled as 0x hex", emit("0x10:\n  nop\n"), "ERR");
    check("a label spelled as decimal", emit("123:\n  nop\n"), "ERR");

    /* And the ones that only look like literals. Hex digits without a suffix
     * are a name; a suffix on something that is not a digit run is a name;
     * one letter is a name even when it is `h`. */
    check("hex digits with no suffix are a label",
          emit("beef:\n  jp beef\n"), "C3 00 00 04");
    check("a trailing h on non-hex is a label",
          emit("zzh:\n  jp zzh\n"), "C3 00 00 04");
    /* Defined but not referenced: `h` is the register H, and `jp h` is
     * refused by both for that reason rather than this one. */
    check("a single h is a label", emit("h:\n  nop\n"), "00");
    check("a suffix in the middle is a label",
          emit("a0h_x:\n  jp a0h_x\n"), "C3 00 00 04");

    /* Local labels: `@name`, belonging to the global label above them.
     *
     * The reference keys one as the enclosing global's name with the local's
     * appended -- its "Label already defined 'outer@aa'" says so -- which
     * makes the same spelling under two globals two different labels, and
     * makes a reference from outside the scope find nothing. dzap reaches the
     * same answers from a table that is emptied at the end of every scope, so
     * these pin the behaviour and not the mechanism. Expected bytes generated
     * from the reference, like every other row here.
     *
     * A local is not tested against the number formats -- the reference
     * returns before that check -- so `@123` and `@0ffh` are labels where the
     * bare spellings are refused a few lines above. */
    check("local under a global",
          emit("outer:\n@loop:\n  jp @loop\n"), "C3 00 00 04");
    check("local before any global",
          emit("  nop\n@loop:\n  jp @loop\n"), "00 C3 01 00 04");
    check("same local name in two scopes",
          emit("one:\n@l:\n  jp @l\ntwo:\n@l:\n  jp @l\n"),
          "C3 00 00 04 C3 04 00 04");
    check("local resolved forward",
          emit("outer:\n  jp @fwd\n@fwd:\n  nop\n"), "C3 04 00 04 00");
    check("local and global of one spelling",
          emit("loop:\n@loop:\n  jp loop\n  jp @loop\n"),
          "C3 00 00 04 C3 00 00 04");
    check("local spelled as a number",
          emit("outer:\n@123:\n  jp @123\n"), "C3 00 00 04");
    check("local spelled as trailing-h hex",
          emit("outer:\n@0ffh:\n  jp @0ffh\n"), "C3 00 00 04");
    check("local as a relative jump",
          emit("outer:\n@l:\n  jr @l\n"), "18 FE");
    /* The at sign is an ordinary name character everywhere but the first
     * position, which is the reference's rule too. */
    check("at sign inside a global name",
          emit("ab@cd:\n  jp ab@cd\n"), "C3 00 00 04");

    /* The four refusals, each of which the reference also refuses. The first
     * three are the scope rule seen from three directions; the last is a
     * redefinition, which has to still be caught inside a table that is
     * emptied and reused. */
    check("local out of scope",
          emit("one:\n@l:\n  nop\ntwo:\n  jp @l\n"), "ERR");
    check("local defined in an earlier scope",
          emit("@aa:\n  nop\nouter:\n  jp @aa\n"), "ERR");
    check("local never defined", emit("outer:\n  jp @nope\n"), "ERR");
    check("local defined twice in one scope",
          emit("outer:\n@l:\n  nop\n@l:\n  nop\n"), "ERR");

    /* A forward local settled at the end of its own scope, with the same name
     * defined again in the next one.
     *
     * This is the case that says local references cannot wait until the end of
     * the source like global ones do. The node behind the first `@x` is handed
     * back when scope `one` closes and is holding scope `two`'s `@x` by the
     * time the source ends, so a reference resolved then would read 0x040005
     * where it should read 0x040004. Both addresses appear in the expected
     * bytes, one line apart, which is what makes the difference visible. */
    check("a forward local settles when its scope does",
          emit("one:\n  jp @x\n@x:\n  nop\ntwo:\n@x:\n  nop\n  jp @x\n"),
          "C3 04 00 04 00 00 C3 05 00 04");

    /* A bucket carried over from the previous scope reads as empty.
     *
     * The stamp is what empties it, and the chain it still holds is not
     * cleared -- so a bucket first *used* in a new scope has to drop that
     * chain rather than link onto it. If it does not, a second name landing in
     * the same bucket walks past its own node into the last scope's, and finds
     * a local that is out of scope and defined, which resolves instead of
     * failing.
     *
     * The colliding name is computed rather than written down, so the test
     * keeps testing this when the key changes. */
    {
        build_tables();
        build_cclass();
        const int want = loc_bucket("@l", 2);
        char other[8];
        bool found = false;
        for (int a = 'a'; a <= 'z' && !found; a++) {
            for (int b = 'a'; b <= 'z' && !found; b++) {
                other[0] = '@';
                other[1] = (char) a;
                other[2] = (char) b;
                other[3] = 0;
                found = loc_bucket(other, 3) == want;
            }
        }
        check_range("a colliding local name exists", found);
        if (found) {
            char src[128];
            snprintf(src, sizeof(src),
                     "one:\n@l:\n  nop\ntwo:\n%s:\n  nop\n  jp @l\n", other);
            check("a bucket left by the last scope is empty", emit(src), "ERR");
        }
    }

    /* And the storage behind it is reused rather than accumulated. 200 scopes
     * of eight locals each: eight fit in one block, so one block is what the
     * program should end up holding however many scopes it has. Without the
     * rewind it would hold twenty-five. */
    {
        /* 200 scopes of eight locals is about 17 KB of source; the buffer is
         * sized from that rather than guessed, and the cursor is clamped
         * because snprintf reports what it would have written, not what it
         * did. */
        static char src[32768];
        size_t at = 0;
        for (int s2 = 0; s2 < 200; s2++) {
            at += (size_t) snprintf(&src[at], sizeof(src) - at, "g%03d:\n", s2);
            check_range("the scope source fits", at < sizeof(src));
            for (int i = 0; i < 8; i++) {
                at += (size_t) snprintf(&src[at], sizeof(src) - at,
                                        "@c%d:\n  nop\n", i);
                check_range("the scope source fits", at < sizeof(src));
            }
        }
        char got[32];
        snprintf(got, sizeof(got), "%d", local_blocks(src));
        check("local storage is reused between scopes", got, "1");
    }

    /* Expressions.
     *
     * The reference evaluates strictly left to right with no precedence at
     * all, so `1+2*3` is 9 and `2*3+1` is 7. Both are here because an
     * evaluator written the way arithmetic is usually written would pass one
     * and fail the other, and passing both is the point. Grouping is `[...]`
     * -- parentheses already mean indirection.
     *
     * Expected bytes generated from the reference, like every other row here.
     */
    check("addition", emit("  ld hl, 1+2\n"), "21 03 00 00");
    check("no precedence, left to right",
          emit("  ld hl, 1+2*3\n"), "21 09 00 00");
    check("and the other way round",
          emit("  ld hl, 2*3+1\n"), "21 07 00 00");
    check("brackets group", emit("  ld hl, 1+[2*3]\n"), "21 07 00 00");
    check("brackets on the left", emit("  ld hl, [1+2]*3\n"), "21 09 00 00");
    check("nested brackets", emit("  ld hl, [[1+2]*3]\n"), "21 09 00 00");
    check("subtraction chains left",
          emit("  ld hl, 10-2-3\n"), "21 05 00 00");
    check("integer division", emit("  ld hl, 100/7\n"), "21 0E 00 00");
    check("shift left", emit("  ld hl, 1<<4\n"), "21 10 00 00");
    check("shift right", emit("  ld a, 8>>2\n"), "3E 02");
    check("bitwise and", emit("  ld hl, 0xFF&0x0F\n"), "21 0F 00 00");
    check("bitwise or", emit("  ld hl, 1|2|4\n"), "21 07 00 00");
    check("bitwise xor", emit("  ld hl, 0xFF^0x0F\n"), "21 F0 00 00");
    check("unary minus", emit("  ld hl, -5+10\n"), "21 05 00 00");
    check("unary complement", emit("  ld hl, ~0\n"), "21 FF FF FF");
    check("spaces around operators", emit("  ld hl, 5 + 3\n"), "21 08 00 00");
    check("a character literal", emit("  ld a, 'A'\n"), "3E 41");
    check("a character literal in a sum", emit("  ld a, 'A'+1\n"), "3E 42");

    /* `$` is the address of the instruction being assembled, and is the same
     * character the reference uses to introduce hex -- `$42` is a number and
     * `$` alone is the address, told apart by what follows. */
    check("dollar is the current address",
          emit("  nop\n  ld hl, $\n"), "00 21 01 00 04");
    check("dollar in a sum", emit("  nop\n  ld hl, $+4\n"), "00 21 05 00 04");

    /* Labels of every kind, as terms. The addresses are the test: `st+2*3` is
     * (st + 2) * 3 and no other reading gives 0x0C0006. */
    check("a label already defined",
          emit("st:\n  nop\n  ld hl, st+2\n"), "00 21 02 00 04");
    check("one label minus another",
          emit("st:\n  nop\nen:\n  ld hl, en-st\n"), "00 21 01 00 00");
    check("a length, then scaled",
          emit("st:\n  nop\nen:\n  ld hl, [en-st]*4\n"), "00 21 04 00 00");
    check("left to right with a label",
          emit("st:\n  nop\n  ld hl, st+2*3\n"), "00 21 06 00 0C");
    check("a local label in an expression",
          emit("one:\n@lp:\n  nop\n  ld hl, @lp+1\n"), "00 21 01 00 04");
    check("an anonymous label in an expression",
          emit("@@:\n  nop\n  ld hl, @b+2\n"), "00 21 02 00 04");

    /* Refusals the reference also makes. A single angle bracket is an error
     * there rather than a comparison. */
    check("a single angle bracket", emit("  ld hl, 1<4\n"), "ERR");
    check("an operator with nothing after", emit("  ld hl, 1+\n"), "ERR");
    check("an unclosed bracket", emit("  ld hl, [1+2\n"), "ERR");

    /* The boundary of what is built. A forward reference on its own is still a
     * fixup and still works; one inside an expression needs the fixup to carry
     * the rest of the sum, which is the next stage. Refused with a message
     * rather than guessed at -- the reference assembles it, so this is a
     * divergence and has to be a loud one. */
    check("a forward label alone still works",
          emit("  ld hl, later\nlater:\n  nop\n"), "21 04 00 04 00");
    check("a forward label in an expression is refused",
          emit("  ld hl, later+1\nlater:\n  nop\n"), "ERR");

    /* Anonymous labels: `@@`, written any number of times and reached by
     * position rather than by name -- `@b`/`@p` for the one above, `@f`/`@n`
     * for the one below. Expected bytes generated from the reference, like
     * every other row here.
     *
     * The addresses are the whole test. Every one of these assembles under any
     * plausible wrong answer too: what says `@b` found the nearer of two `@@`,
     * or that `@f` on a line that defines one means the *next* one and not
     * itself, is which address comes out. */
    check("backward to the @@ above",
          emit("@@:\n  nop\n  jp @b\n"), "00 C3 00 00 04");
    check("forward to the @@ below",
          emit("  jp @f\n@@:\n  nop\n"), "C3 04 00 04 00");
    /* An anonymous label takes effect at once, unlike a global, whose scope
     * starts on the next line: this jumps to itself. */
    check("@@ and a reference on one line", emit("@@: jp @b\n"), "C3 00 00 04");
    check("two @@, backward takes the nearer",
          emit("@@:\n  nop\n@@:\n  nop\n  jp @b\n"), "00 00 C3 01 00 04");
    check("two @@, forward takes the nearer",
          emit("  jp @f\n@@:\n  nop\n@@:\n  nop\n"), "C3 04 00 04 00 00");
    /* And on a line that defines one, forward means the one after it. */
    check("forward from between two @@",
          emit("@@:\n  jp @f\n@@:\n  nop\n"), "C3 04 00 04 00");
    check("backward from between two @@",
          emit("@@:\n  nop\n  jp @b\n@@:\n  nop\n"), "00 C3 00 00 04 00");
    check("many @f before one @@",
          emit("  jp @f\n  jp @f\n@@:\n  nop\n"),
          "C3 08 00 04 C3 08 00 04 00");
    check("@@ twice with nothing between",
          emit("@@:\n@@:\n  jp @b\n"), "C3 00 00 04");
    check("both spellings of each direction",
          emit("  jp @n\n@@:\n  jp @p\n"), "C3 04 00 04 C3 04 00 04");
    check("the spelling is case-insensitive",
          emit("  jp @F\n@@:\n  nop\n"), "C3 04 00 04 00");
    /* Not name-scoped: a global label between them changes nothing, and they
     * do not disturb the local table either. */
    check("a global label between two anonymous ones",
          emit("@@:\n  nop\nmid:\n  nop\n  jp @b\n"), "00 00 C3 00 00 04");
    check("anonymous and local labels together",
          emit("one:\n@l:\n@@:\n  nop\n  jp @b\n  jp @l\n"),
          "00 C3 00 00 04 C3 00 00 04");
    /* Every operand position, not just a jump target. */
    check("@f as an immediate", emit("  ld hl, @f\n@@:\n  nop\n"),
          "21 04 00 04 00");
    check("@f inside parentheses", emit("  ld a, (@f)\n@@:\n  nop\n"),
          "3A 04 00 04 00");
    check("a relative jump backward", emit("@@:\n  nop\n  jr @b\n"), "00 18 FD");
    check("a relative jump forward", emit("  jr @f\n@@:\n  nop\n"), "18 00 00");

    /* The refusals. `@@` has no name, so it is not something a reference can
     * name; and a direction with nothing in it is an error rather than zero. */
    check("@b with no @@ above it", emit("  jp @b\n"), "ERR");
    check("@f with no @@ below it", emit("@@:\n  nop\n  jp @f\n"), "ERR");
    check("@@ as a reference", emit("@@:\n  nop\n  jp @@\n"), "ERR");

    /* Only the exact two-character spellings are reserved. Three characters is
     * an ordinary local -- and a local really may be called `@f`, which the
     * reference accepts and then leaves unreachable, because `@f` in an
     * operand is the anonymous one. */
    check("@ff is an ordinary local",
          emit("one:\n@ff:\n  nop\n  jp @ff\n"), "00 C3 00 00 04");
    check("@bb is an ordinary local",
          emit("one:\n@bb:\n  nop\n  jp @bb\n"), "00 C3 00 00 04");
    check("a local named @f cannot be reached",
          emit("one:\n@f:\n  nop\n@@:\n  jp @f\n"), "ERR");
    /* But a local may still be called `@bb`: only the two-character
     * spellings are reserved. */
    check("a local whose name starts with b",
          emit("outer:\n@bb:\n  jp @bb\n"), "C3 00 00 04");

    /* A global label and an instruction on one line: the scope the label opens
     * starts with the *next* line, so the operand here still belongs to the
     * scope being closed.
     *
     * Both directions, because ending the scope at the label gets both wrong
     * in opposite ways -- it refuses the first of these, which the reference
     * assembles, and assembles the second, which the reference refuses. The
     * bytes of the first are what pin it: 0x040000 is scope `one`'s @l, and
     * there is no other address it could resolve to. */
    check("a global label switches scope from the next line",
          emit("one:\n@l:\n  nop\ntwo: jp @l\n"), "00 C3 00 00 04");
    check("and the label it opens is not in scope on its own line",
          emit("one:\n  nop\ntwo: jp @l\n@l:\n  nop\n"), "ERR");
    /* On its own line it still switches, which is the ordinary case and the
     * one the deferral must not break. */
    check("a global on its own line still switches scope",
          emit("one:\n@l:\n  nop\ntwo:\n  jp @l\n"), "ERR");

    /* Sixteen locals in one scope and then a second scope reusing the same
     * storage under the same names. The addresses are what say the reuse did
     * not carry anything over. */
    check("a scope reused under the same names",
          emit("one:\n"
               "@a01:\n@a02:\n@a03:\n@a04:\n@a05:\n@a06:\n@a07:\n@a08:\n"
               "@a09:\n@a10:\n@a11:\n@a12:\n@a13:\n@a14:\n@a15:\n@a16:\n"
               "  jp @a01\n"
               "two:\n"
               "@a01:\n  nop\n"
               "  jp @a01\n"), "C3 00 00 04 00 C3 04 00 04");

    /* And past the first block, which holds 64. The whole Agon corpus has at
     * most 20 locals in one scope, so nothing real reaches here -- but the
     * second block is allocated on a different path from the first and then
     * rewound to the first when the scope ends, and neither of those is
     * exercised by anything above. Built rather than written out so the count
     * cannot drift away from LOCS_STEP. */
    {
        static char src[2048];
        int at = snprintf(src, sizeof(src), "one:\n");
        for (int i = 0; i < LOCS_STEP + 6; i++) {
            at += snprintf(&src[at], sizeof(src) - (size_t) at, "@b%03d:\n", i);
        }
        snprintf(&src[at], sizeof(src) - (size_t) at,
                 "  jp @b000\ntwo:\n@b000:\n  nop\n  jp @b000\n");
        check("more locals than one block holds", emit(src),
              "C3 00 00 04 00 C3 04 00 04");
    }

    /* A name ending in b is not binary unless what precedes it is. The test
     * rejects on two characters before anything walks the name -- a number is
     * a leading digit or a trailing h or b, because $, # and % are not
     * mnemonic characters and cannot appear in a token that got this far --
     * and these are the words that trip that shortcut. */
    check("a name ending in b is a label", emit("grab:\n  jp grab\n"),
          "C3 00 00 04");
    check("another name ending in b", emit("club:\n  jp club\n"),
          "C3 00 00 04");
    check("a digit and a b is binary", emit("10b:\n  nop\n"), "ERR");

    /* A leading digit does not make a number, which cost four of twenty
     * probes before it was checked: 2 is not a binary digit, so `2b` is a
     * name, and so are `1z`, `5g` and `123abc`. The reference takes all four
     * as labels. Only the general parser can decide, and the two-character
     * rejection above is what keeps it off the common path. */
    check("a digit and a non-binary b is a label",
          emit("2b:\n  jp 2b\n"), "C3 00 00 04");
    check("a digit and a letter is a label",
          emit("1z:\n  jp 1z\n"), "C3 00 00 04");
    check("digits then letters is a label",
          emit("123abc:\n  jp 123abc\n"), "C3 00 00 04");
    check("0b with a non-binary digit is a label",
          emit("0b12:\n  jp 0b12\n"), "C3 00 00 04");
    check("0b alone is binary zero", emit("0b:\n  nop\n"), "ERR");

    /* And they can be referenced, not only defined. The operand parser asked
     * whether the token started with a letter, which got `$42`, `#42` and
     * `%1010` right -- they are literals starting with neither -- and these
     * wrong. It now asks whether any radix accepts the token, which is the
     * same question the definition asks. */
    check("a digit-leading label can be referenced",
          emit("2b:\n  jp 2b\n"), "C3 00 00 04");
    check("the radix prefixes are still literals",
          emit("  ld a, $42\n  ld a, #42\n  ld a, %1010\n"),
          "3E 42 3E 42 3E 0A");

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
