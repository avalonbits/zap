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
        case WHITE_SPACE: return "WS";
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
        case SEMI_COLON:  return "SEMI";
        case HASH:        return "HASH";
        case DOLLAR:      return "DOLLAR";
        case B_SLASH:     return "BS";
        case F_SLASH:     return "FS";
        case NAME:        return "NAME";
        case NUMBER:      return "NUM";
        case HEX_NUMBER:  return "HEX";
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
        if (tk.tk_ == WHITE_SPACE) {
            continue;
        }
        if (n > 0 && n < (int) sizeof(out) - 1) {
            out[n++] = ' ';
        }
        n += snprintf(&out[n], sizeof(out) - n, "%s(%.*s)",
                      tk_name(tk.tk_), tk.sz_, tk.txt_);
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
            "INSN(ld) REG(a) COMMA(,) NUM(255) NL(\n)");
    check_s("dollar hex literal",
            lex_all("ld a,$FF\n"),
            "INSN(ld) REG(a) COMMA(,) HEX(FF) NL(\n)");
    check_s("all-digit h-suffix hex",
            lex_all("ld a,12h\n"),
            "INSN(ld) REG(a) COMMA(,) HEX(12) NL(\n)");

    /* Punctuation the instruction parsers rely on. */
    check_s("indirect operand punctuation",
            lex_all("ld a,(hl)\n"),
            "INSN(ld) REG(a) COMMA(,) LP(() REG(hl) RP()) NL(\n)");
    check_s("suffix dot",
            lex_all("rst.lil 10h\n"),
            "INSN(rst) DOT(.) NAME(lil) HEX(10) NL(\n)");

    /* A directive is only a directive to the lexer -- the dot is separate. */
    check_s("dot directive",
            lex_all(".org $400000\n"),
            "DOT(.) DIR(org) HEX(400000) NL(\n)");

    /* Comments are the parser's job: the lexer just emits the semicolon and
     * whatever follows it. This is what makes the parser's comment state
     * machine load-bearing. */
    check_s("comment is not stripped by the lexer",
            lex_all("ret ; done\n"),
            "INSN(ret) SEMI(;) NAME(done) NL(\n)");

    /* Strings arrive as delimiters plus their contents. */
    check_s("quoted string delimiters",
            lex_all(".db \"Hi\"\n"),
            "DOT(.) DIR(db) DQ(\") NAME(Hi) DQ(\") NL(\n)");

    /* A file with no trailing newline must still terminate. */
    check_s("no trailing newline",
            lex_all("nop"),
            "INSN(nop)");

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
