/*
 * Host tests for the lexer's token stream.
 *
 * These lock down the parts of lexing that are correct today, so the move from
 * CEdev to agondev -- and the number-parsing rewrite that follows it -- cannot
 * quietly change them. They deliberately do not assert the current handling of
 * 0A0h, 0x1F, %1010 or negative literals: those are broken, and encoding the
 * broken results here would make the fix look like a regression.
 *
 * The lexer reads from a file, so each case is written to a scratch file first.
 * Run under ASan (see test/run.sh) -- the token buffer is unbounded, and a long
 * name is the way that shows up.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lexer.h"

static int failures = 0;

static void check_s(const char* name, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-44s %s\n", name, "ok");
    } else {
        fprintf(stderr, "FAIL  %-44s\n        got  \"%s\"\n        want \"%s\"\n",
                name, got, want);
        failures++;
    }
}

static const char* tk_name(TOKEN t) {
    switch (t) {
        case NONE:        return "NONE";
        case UNKNOWN:     return "UNK";
        case NEW_LINE:    return "NL";
        case EQUALS:      return "EQ";
        case PLUS:        return "PLUS";
        case MINUS:       return "MINUS";
        case QUOTE:       return "Q";
        case D_QUOTE:     return "DQ";
        case L_PAREN:     return "LP";
        case R_PAREN:     return "RP";
        case COMMA:       return "COMMA";
        case DOT:         return "DOT";
        case COLON:       return "COLON";
        case HASH:        return "HASH";
        case DOLLAR:      return "DOLLAR";
        case B_SLASH:     return "BS";
        case F_SLASH:     return "FS";
        case STAR:        return "STAR";
        case AMPERSAND:   return "AMP";
        case PIPE:        return "PIPE";
        case CARET:       return "CARET";
        case TILDE:       return "TILDE";
        case SHIFT_L:     return "SHL";
        case SHIFT_R:     return "SHR";
        case L_BRACKET:   return "LB";
        case R_BRACKET:   return "RB";
        case BAD_LITERAL: return "BAD";
        case NAME:        return "NAME";
        case NUMBER:      return "NUM";
        case DIRECTIVE:   return "DIR";
        case INSTRUCTION: return "INSN";
        case REGISTER:    return "REG";
        case FLAG:        return "FLAG";
        default:          return "?";
    }
}

/* The whole token stream as one string, so a case reads as a single
 * assertion. Whitespace is dropped -- it is not what any of these are about. */
static const char* lex_all(const char* src) {
    static char out[1024];
    char path[] = "/tmp/zap_lex_XXXXXX";
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

    lexer lex;
    if (lex_init(&lex, path) == NULL) {
        unlink(path);

        return "<open failed>";
    }

    int n = 0;
    out[0] = 0;
    for (token tk = lex_next(&lex); tk.tk_ != NONE; tk = lex_next(&lex)) {
        if (n > 0 && n < (int) sizeof(out) - 1) {
            out[n++] = ' ';
        }
        if (tk.tk_ == NUMBER) {
            n += snprintf(&out[n], sizeof(out) - n, "NUM(%.*s=%ld)",
                          tk.sz_, tk.txt_, (long) tk.val_);
        } else {
            n += snprintf(&out[n], sizeof(out) - n, "%s(%.*s)",
                          tk_name(tk.tk_), tk.sz_, tk.txt_);
        }
        if (n >= (int) sizeof(out) - 1) {
            break;
        }
    }
    out[n] = 0;

    br_destroy(&lex.rd_);
    unlink(path);

    return out;
}

int main(void) {
    /* Mnemonics, registers and flags resolve through the reserved tables, and
     * the lookup is case-insensitive. */
    check_s("instruction and register",
            lex_all("ld a,b\n"),
            "INSN(ld) REG(a) COMMA(,) REG(b) NL(\n)");
    check_s("uppercase resolves the same",
            lex_all("LD A,B\n"),
            "INSN(LD) REG(A) COMMA(,) REG(B) NL(\n)");
    check_s("mixed case resolves the same",
            lex_all("Ld Hl,Bc\n"),
            "INSN(Ld) REG(Hl) COMMA(,) REG(Bc) NL(\n)");
    check_s("flag is not a register",
            lex_all("ret nz\n"),
            "INSN(ret) FLAG(nz) NL(\n)");

    /* A label is a bare NAME; the parser is what gives the colon meaning. */
    check_s("label and colon",
            lex_all("start:\n"),
            "NAME(start) COLON(:) NL(\n)");
    check_s("underscore and digits in a name",
            lex_all("_lbl_2:\n"),
            "NAME(_lbl_2) COLON(:) NL(\n)");

    /* The two numeric forms that are correct today. */
    check_s("decimal literal",
            lex_all("ld a,255\n"),
            "INSN(ld) REG(a) COMMA(,) NUM(255=255) NL(\n)");
    check_s("dollar hex literal",
            lex_all("ld a,$FF\n"),
            "INSN(ld) REG(a) COMMA(,) NUM($FF=255) NL(\n)");
    check_s("all-digit h-suffix hex",
            lex_all("ld a,12h\n"),
            "INSN(ld) REG(a) COMMA(,) NUM(12h=18) NL(\n)");

    /* Punctuation the instruction parsers rely on. */
    check_s("indirect operand punctuation",
            lex_all("ld a,(hl)\n"),
            "INSN(ld) REG(a) COMMA(,) LP(() REG(hl) RP()) NL(\n)");
    check_s("suffix dot",
            lex_all("rst.lil 10h\n"),
            "INSN(rst) DOT(.) NAME(lil) NUM(10h=16) NL(\n)");

    /* A directive is only a directive to the lexer -- the dot is separate. */
    check_s("dot directive",
            lex_all(".org $400000\n"),
            "DOT(.) DIR(org) NUM($400000=4194304) NL(\n)");

    /* A comment never becomes tokens, and does not even become a semicolon:
     * it ends the statement, which is all the parser ever did with it. The
     * body used to arrive word by word, each word paying for a scan, a hash
     * and a reserved-table lookup before being discarded -- on a source that
     * is a quarter comments, a quarter of the lexer's work meant nothing. */
    check_s("comment lexes as the end of the line",
            lex_all("ret ; done\n"),
            "INSN(ret) NL(\n)");

    /* The line still ends exactly once, so the statement after it is intact
     * and the line count stays right. */
    check_s("comment ends the line exactly once",
            lex_all("nop ; a\nret\n"),
            "INSN(nop) NL(\n) INSN(ret) NL(\n)");

    /* Text that would otherwise lex as something meaningful -- a directive
     * name, an unterminated string or character literal -- is inert inside a
     * comment. The directive case is not hypothetical: "; starting at byte
     * 64." was once enough to break a file, because "byte" reached the parser
     * as a real directive. */
    check_s("directive name inside a comment",
            lex_all("nop ; starting at byte 64.\n"),
            "INSN(nop) NL(\n)");
    check_s("unbalanced quote inside a comment",
            lex_all("nop ; it's \"fine\n"),
            "INSN(nop) NL(\n)");

    /* A comment on its own line is an empty line. */
    check_s("whole-line comment",
            lex_all("; just a comment\nnop\n"),
            "NL(\n) INSN(nop) NL(\n)");

    /* One closing the file with no trailing newline still terminates the
     * statement rather than running off the end. */
    check_s("comment at end of file with no newline",
            lex_all("nop ; trailing"),
            "INSN(nop) NL(\n)");

    /* Strings arrive as delimiters plus their contents. */
    check_s("quoted string delimiters",
            lex_all(".db \"Hi\"\n"),
            "DOT(.) DIR(db) DQ(\") NAME(Hi) DQ(\") NL(\n)");

    /* A file with no trailing newline must still terminate. */
    check_s("no trailing newline",
            lex_all("nop"),
            "INSN(nop)");

    /* A minus is an operator, not part of the number. The old lexer folded
     * the digits into the token, which is why label-2 and $-2 could not be
     * written at all. */
    check_s("minus is an operator",
            lex_all("ld a,-5\n"),
            "INSN(ld) REG(a) COMMA(,) MINUS(-) NUM(5=5) NL(\n)");
    check_s("current pc stays a dollar",
            lex_all("jr $-2\n"),
            "INSN(jr) DOLLAR($) MINUS(-) NUM(2=2) NL(\n)");
    check_s("bare dollar",
            lex_all("jp $\n"),
            "INSN(jp) DOLLAR($) NL(\n)");

    /* The literal forms that used to split into a number and a name. */
    check_s("letter-initial hex is one token",
            lex_all("db C0h\n"),
            "DIR(db) NUM(C0h=192) NL(\n)");
    check_s("hex with letters is one token",
            lex_all("ld a,1Fh\n"),
            "INSN(ld) REG(a) COMMA(,) NUM(1Fh=31) NL(\n)");
    check_s("c-style hex",
            lex_all("ld a,0x1F\n"),
            "INSN(ld) REG(a) COMMA(,) NUM(0x1F=31) NL(\n)");
    check_s("percent binary",
            lex_all("ld d,%10101010\n"),
            "INSN(ld) REG(d) COMMA(,) NUM(%10101010=170) NL(\n)");
    check_s("hash hex",
            lex_all("ld a,#A0\n"),
            "INSN(ld) REG(a) COMMA(,) NUM(#A0=160) NL(\n)");
    check_s("binary suffix",
            lex_all("ld h,1010b\n"),
            "INSN(ld) REG(h) COMMA(,) NUM(1010b=10) NL(\n)");
    check_s("invalid binary stays a name",
            lex_all("ld h,12b\n"),
            "INSN(ld) REG(h) COMMA(,) NAME(12b) NL(\n)");

    /* Character literals carry their value; the text is kept only so a bad
     * one can be shown back to the user. */
    check_s("character literal",
            lex_all("ld a,'a'\n"),
            "INSN(ld) REG(a) COMMA(,) NUM('a'=97) NL(\n)");
    check_s("escaped character literal",
            lex_all("ld a,'\\n'\n"),
            "INSN(ld) REG(a) COMMA(,) NUM('\\n'=10) NL(\n)");
    check_s("escaped quote",
            lex_all("db '\\''\n"),
            "DIR(db) NUM('\\''=39) NL(\n)");
    /* A backslash followed by the closing quote is a literal backslash, told
     * apart from an escaped quote only by whether another quote follows. The
     * reference's corpus pins both spellings to 0x5C. */
    check_s("lone backslash literal",
            lex_all("db '\\'\n"),
            "DIR(db) NUM('\\'=92) NL(\n)");
    check_s("escaped backslash",
            lex_all("db '\\\\'\n"),
            "DIR(db) NUM('\\\\'=92) NL(\n)");

    /* The literal errors the reference rejects. Lexing resumes after a bad
     * literal rather than swallowing the line, so the trailing quote of a
     * two-character literal comes back as a second bad token. */
    check_s("empty literal",
            lex_all("ld a,''\n"),
            "INSN(ld) REG(a) COMMA(,) BAD('') NL(\n)");
    check_s("unterminated literal",
            lex_all("ld a,'a\n"),
            "INSN(ld) REG(a) COMMA(,) BAD('a) NL(\n)");
    check_s("two-character literal",
            lex_all("ld a,'ab'\n"),
            "INSN(ld) REG(a) COMMA(,) BAD('a) REG(b) BAD(') NL(\n)");
    check_s("bad escape",
            lex_all("ld a,'\\+'\n"),
            "INSN(ld) REG(a) COMMA(,) BAD('\\+) BAD(') NL(\n)");

    /* Expression operators, including the two-character shifts and the
     * brackets that group (parentheses cannot -- they mean indirection). */
    check_s("arithmetic operators",
            lex_all("db 1+2*3-4/5\n"),
            "DIR(db) NUM(1=1) PLUS(+) NUM(2=2) STAR(*) NUM(3=3) "
            "MINUS(-) NUM(4=4) FS(/) NUM(5=5) NL(\n)");
    check_s("bitwise and shifts",
            lex_all("db 1<<2>>3&4|5^6~7\n"),
            "DIR(db) NUM(1=1) SHL(<<) NUM(2=2) SHR(>>) NUM(3=3) "
            "AMP(&) NUM(4=4) PIPE(|) NUM(5=5) CARET(^) NUM(6=6) TILDE(~) NUM(7=7) NL(\n)");
    check_s("brackets group",
            lex_all("db [1+2]\n"),
            "DIR(db) LB([) NUM(1=1) PLUS(+) NUM(2=2) RB(]) NL(\n)");

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
