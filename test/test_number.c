/*
 * Host tests for numeric literal conversion.
 *
 * The cases come from the reference assembler's own Numbers corpus, because
 * zap has to agree with it byte for byte. Several of the forms overlap, and
 * the order they are tried in decides the answer -- 0bh is hex 0x0B, not
 * binary, and a lone 'a' is a register rather than a hex digit. Those are the
 * cases worth having here; the long runs of 00h..ffh in the corpus prove
 * nothing extra once the suffix rule is right.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "value.h"

static int failures = 0;

static void ok(const char* txt, value want) {
    value got = 0;
    if (!num_parse(txt, (int) strlen(txt), &got)) {
        fprintf(stderr, "FAIL  %-16s rejected, want 0x%X\n", txt, (unsigned) want);
        failures++;

        return;
    }
    if (got != want) {
        fprintf(stderr, "FAIL  %-16s got 0x%X, want 0x%X\n",
                txt, (unsigned) got, (unsigned) want);
        failures++;

        return;
    }
    fprintf(stderr, "PASS  %-16s 0x%X\n", txt, (unsigned) got);
}

static void rejected(const char* txt) {
    value got = 0;
    if (num_parse(txt, (int) strlen(txt), &got)) {
        fprintf(stderr, "FAIL  %-16s accepted as 0x%X, want rejected\n",
                txt, (unsigned) got);
        failures++;

        return;
    }
    fprintf(stderr, "PASS  %-16s rejected\n", txt);
}

int main(void) {
    /* Hex, every prefix and both cases. */
    ok("0xa", 0x0A); ok("0xA", 0x0A); ok("0XA", 0x0A);
    ok("0x0A", 0x0A); ok("0X0A", 0x0A); ok("0x00A", 0x0A);
    ok("#A", 0x0A);  ok("#0A", 0x0A);  ok("#00A", 0x0A);
    ok("$A", 0x0A);  ok("$0A", 0x0A);  ok("$00A", 0x0A);
    ok("$400000", 0x400000);
    ok("$B0000", 0x0B0000);

    /* Hex, 'h' suffix -- including the letter-initial forms that zap used to
     * split into a number and a name. */
    ok("Ah", 0x0A); ok("AH", 0x0A); ok("ah", 0x0A);
    ok("0Ah", 0x0A); ok("0AH", 0x0A); ok("00Ah", 0x0A);
    ok("C0h", 0xC0); ok("84h", 0x84); ok("1Fh", 0x1F);
    ok("a0h", 0xA0); ok("ffh", 0xFF); ok("FFh", 0xFF);
    ok("fh", 0x0F);  ok("0h", 0x00);

    /* The overlap that decides the whole ordering: the 'h' suffix is claimed
     * before the '0b' binary prefix. */
    ok("0bh", 0x0B); ok("0bH", 0x0B);
    ok("0b0h", 0xB0); ok("0b0H", 0xB0);
    ok("0b1h", 0xB1); ok("0b1H", 0xB1);

    /* Binary, both prefix and suffix. */
    ok("0b0", 0); ok("0B1", 1); ok("0b01", 1); ok("0b10", 2);
    ok("0b11111111", 0xFF); ok("0B00000000", 0);
    ok("%0", 0); ok("%1", 1); ok("%10", 2); ok("%11111111", 0xFF);
    ok("1b", 1); ok("1B", 1); ok("10b", 2); ok("11111111b", 0xFF);
    ok("0b", 0);   /* binary prefix, no digits -- the reference emits 0 */
    ok("0x", 0);   /* likewise for the hex prefix */

    /* Decimal, including leading zeros. */
    ok("0", 0); ok("00", 0); ok("09", 9); ok("128", 128); ok("255", 255);
    ok("42", 42); ok("64", 64);

    /* Too wide for 24 bits: wraps rather than failing, so the operand range
     * check is what rejects it -- same as the reference. */
    ok("0x1000000", 0x1000000);

    /* Register and flag names must not be readable as literals, or the lexer
     * would hand back a number where an operand was meant. A lone character is
     * decimal for exactly this reason. */
    rejected("a"); rejected("b"); rejected("c"); rejected("d");
    rejected("e"); rejected("f"); rejected("h"); rejected("l");
    rejected("af"); rejected("bc"); rejected("de"); rejected("hl");
    rejected("ix"); rejected("iy"); rejected("sp");
    rejected("nz"); rejected("z"); rejected("nc"); rejected("pe");

    /* Ordinary identifiers. */
    rejected("_start"); rejected("loop"); rejected("hello_world");
    rejected("@loop"); rejected("prstr");
    rejected("ab");     /* 'b' suffix, but 'a' is not a binary digit */
    rejected("2h3");    /* suffix must be last */
    rejected("");
    rejected("$");      /* bare $ is the program counter, not a literal */
    rejected("#");
    rejected("%");

    /* Escapes, which share the literal path. */
    char c = 0;
    struct { char in; char want; } escapes[] = {
        {'a', 0x07}, {'b', 0x08}, {'e', 0x1B}, {'f', 0x0C}, {'n', 0x0A},
        {'r', 0x0D}, {'t', 0x09}, {'v', 0x0B}, {'\\', 0x5C}, {'\'', 0x27},
        {'"', 0x22}, {'?', 0x3F}, {'0', 0x00},
    };
    for (unsigned i = 0; i < sizeof(escapes) / sizeof(escapes[0]); i++) {
        if (!esc_char(escapes[i].in, &c) || c != escapes[i].want) {
            fprintf(stderr, "FAIL  escape \\%c got 0x%02X, want 0x%02X\n",
                    escapes[i].in, (unsigned char) c, (unsigned char) escapes[i].want);
            failures++;
        }
    }
    /* The two escapes the reference rejects, from Errors_literals. */
    if (esc_char(' ', &c)) { fprintf(stderr, "FAIL  escape '\\ ' accepted\n"); failures++; }
    if (esc_char('+', &c)) { fprintf(stderr, "FAIL  escape '\\+' accepted\n"); failures++; }
    fprintf(stderr, "PASS  %-16s ok\n", "escapes");

    /* The cheap guard that lets num_parse reject a name without scanning it
     * looks only at the first and last character. Rejecting too eagerly would
     * be silent and wrong -- the text would come back as a name and resolve
     * against a symbol that does not exist -- so the boundary is pinned from
     * both sides here.
     *
     * These must survive it: the letter-initial forms are literals only
     * because of their final character, which is the whole reason the guard
     * cannot just test the first one. */
    ok("Ah", 0x0A);       /* letter first, 'h' last  */
    ok("ffh", 0xFF);      /* all letters             */
    ok("1010b", 0x0A);    /* digit first, 'b' last: binary suffix */
    ok("$FF", 0xFF);      /* prefix form             */
    ok("%1010", 0x0A);    /* prefix form             */
    ok("9", 9);           /* lone digit              */
    ok("0b1h", 0xB1);     /* the suffix/prefix overlap */

    /* And these must still be rejected, as they were before the guard: the
     * guard only ever declines to scan, it never accepts. */
    rejected("hello");          /* neither digit-initial nor h/b-final */
    rejected("loop");
    rejected("_start");
    rejected("push");           /* ends in 'h' -- the guard lets it through, and
                           * scan_base still has to reject it */
    rejected("sub");            /* ends in 'b', same */
    rejected("label_b");
    rejected("g");              /* lone non-digit */
    rejected("fb");             /* 'b' suffix, but 'f' is not a binary digit */

    /* Ending in 'h' is not enough to be hex: everything before the suffix has
     * to be a hex digit too. Only an identifier spelled entirely from 0-9a-f
     * is at risk of being read as a number, which is the reference's rule as
     * well -- ez80asm rejects Beefh, ABCh, Fh, dh and Ah as label names, and
     * accepts Ansh, cache, loop_h, hash, push and xh, exactly as zap does. */
    rejected("Ansh");           /* 'n' and 's' are not hex digits */
    rejected("cache");
    rejected("loop_h");
    rejected("xh");
    rejected("tab");
    ok("Beefh", 0xBEEF);        /* all hex digits: this one really is a number */
    ok("ABCh", 0xABC);
    ok("dh", 0x0D);

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
