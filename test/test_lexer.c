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
#include "isa.h"
#include "value.h"
#include "parser.h"

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
    token tk;
    for (lex_next(&lex, &tk); tk.tk_ != NONE; lex_next(&lex, &tk)) {
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

/* Lexes a single word and reports the kind and type index it was given. */
static bool classify(const char* word, TOKEN* tk, TK_TYPE* tt) {
    char path[] = "/tmp/zap_cls_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return false;
    }
    const size_t n = strlen(word);
    if (write(fd, word, n) != (long) n) {
        close(fd);
        unlink(path);

        return false;
    }
    close(fd);

    lexer lex;
    if (lex_init(&lex, path) == NULL) {
        unlink(path);

        return false;
    }
    token t;
    lex_next(&lex, &t);
    *tk = t.tk_;
    *tt = t.tt_;
    br_destroy(&lex.rd_);
    unlink(path);

    return true;
}

/* Registers and flags are matched directly rather than looked up, and that
 * shortcut takes precedence over the reserved-word table.
 *
 * It is only safe while no mnemonic shares a name with a register or a flag.
 * The instruction table is generated, so that is not something to assume once
 * and forget: if a future eZ80 revision adds a mnemonic called "P" or "MB",
 * the table would have overridden the flag and the direct match will not, and
 * the assembler changes meaning with nothing to say so.
 *
 * This walks the generated table and asserts every mnemonic in it still lexes
 * as an instruction. It is the check the shortcut's correctness rests on. */
static void check_no_mnemonic_shadowed(void) {
    int shadowed = 0;
    for (int i = 0; i < isa_table_count; i++) {
        TOKEN tk = NONE;
        TK_TYPE tt = TY_NONE;
        if (!classify(isa_table[i].name, &tk, &tt)) {
            fprintf(stderr, "FAIL  could not lex mnemonic %s\n", isa_table[i].name);
            failures++;

            return;
        }
        if (tk != INSTRUCTION) {
            fprintf(stderr, "FAIL  mnemonic %s lexes as %s, not INSTRUCTION\n",
                    isa_table[i].name, tk_name(tk));
            shadowed++;
        }
    }
    if (shadowed == 0) {
        fprintf(stderr, "PASS  %-44s %d mnemonics\n",
                "no mnemonic shadowed by a register or flag", isa_table_count);
    } else {
        failures += shadowed;
    }
}

/* Every register and flag, in both cases, with the type index each carries --
 * a shortcut that returned the right kind and the wrong index would assemble
 * to different bytes with nothing else in the suite noticing. */
static void check_reg(const char* word, TOKEN want_tk, TK_TYPE want_tt) {
    TOKEN tk = NONE;
    TK_TYPE tt = TY_NONE;
    char upper[8];
    size_t n = strlen(word);
    for (size_t i = 0; i < n && i < sizeof(upper) - 1; i++) {
        upper[i] = (char) (word[i] >= 'a' && word[i] <= 'z' ? word[i] - 32 : word[i]);
    }
    upper[n] = 0;

    if (!classify(word, &tk, &tt) || tk != want_tk || tt != want_tt) {
        fprintf(stderr, "FAIL  %-44s lower case\n", word);
        failures++;

        return;
    }
    if (!classify(upper, &tk, &tt) || tk != want_tk || tt != want_tt) {
        fprintf(stderr, "FAIL  %-44s upper case\n", word);
        failures++;

        return;
    }
    fprintf(stderr, "PASS  %-44s %s\n", word, tk_name(want_tk));
}

int main(void) {
    /* The lexer's buffer is not free to grow. Every source suspended by an
     * .include keeps its buffer while it waits, so a full include stack holds
     * MAX_INCLUDE_DEPTH + 1 of them at the same time. At 64 KiB -- which this
     * briefly was -- that is 576 KiB, more than an Agon Light's 512 KiB of
     * SRAM, so a deeply nested source could not be assembled at all.
     *
     * The speed argument does not justify it either: across 8, 16, 32 and 64
     * KiB the whole range is within 0.04% on the host, and past 16 it is a
     * slight loss on sources made of many small files. 16 is what the Agon
     * sweep settled on and what the change was benchmarked at. */
    {
        const int worst_kb = LEX_BUF_KB * (MAX_INCLUDE_DEPTH + 1);
        fprintf(stderr, "      %d KiB buffer x %d sources = %d KiB\n",
                LEX_BUF_KB, MAX_INCLUDE_DEPTH + 1, worst_kb);
        check_s("lexer buffers fit an Agon with room to spare",
                worst_kb <= 192 ? "ok" : "too big", "ok");
    }

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
     * statement rather than running off the end. It carries no text: token
     * text points into the source now, and there is no newline in the source
     * to point at. The parser keys on the token rather than its text, and
     * already handles a zero-length NEW_LINE -- it makes one itself at the end
     * of an included file. */
    check_s("comment at end of file with no newline",
            lex_all("nop ; trailing"),
            "INSN(nop) NL()");

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

    check_no_mnemonic_shadowed();

    /* Registers and flags are matched before the literal test, so the two
     * orders only agree while no register or flag name also parses as a
     * number. The single-character forms are forced to decimal precisely so
     * a..f are not read as hex digits, and mb, ixh and iyh reach scan_base and
     * fail on their first character -- none of that is obvious enough to leave
     * to the eye, and getting it wrong turns a register into a literal
     * silently. */
    {
        static const char* regs[] = {"a","b","c","d","e","f","h","l","i","r",
                                     "af","bc","de","hl","ix","iy","sp","mb",
                                     "ixh","ixl","iyh","iyl",
                                     "z","p","m","nz","nc","po","pe", 0};
        int bad = 0;
        for (int i = 0; regs[i]; i++) {
            value v = 0;
            char up[8];
            int n = 0;
            for (const char* q = regs[i]; *q; q++) {
                up[n++] = (char) (*q - 32);
            }
            up[n] = 0;
            if (num_parse(regs[i], n, &v) || num_parse(up, n, &v)) {
                fprintf(stderr, "FAIL  %s parses as a number\n", regs[i]);
                bad++;
            }
        }
        if (bad == 0) {
            fprintf(stderr, "PASS  %-44s 29 names\n",
                    "no register or flag parses as a number");
        } else {
            failures += bad;
        }
    }

    check_reg("a", REGISTER, REG_A);
    check_reg("b", REGISTER, REG_B);
    check_reg("c", REGISTER, REG_C);
    check_reg("d", REGISTER, REG_D);
    check_reg("e", REGISTER, REG_E);
    check_reg("f", REGISTER, REG_F);
    check_reg("h", REGISTER, REG_H);
    check_reg("l", REGISTER, REG_L);
    check_reg("i", REGISTER, REG_I);
    check_reg("r", REGISTER, REG_RR);
    check_reg("af", REGISTER, REG_AF);
    check_reg("bc", REGISTER, REG_BC);
    check_reg("de", REGISTER, REG_DE);
    check_reg("hl", REGISTER, REG_HL);
    check_reg("ix", REGISTER, REG_IX);
    check_reg("iy", REGISTER, REG_IY);
    check_reg("sp", REGISTER, REG_SP);
    check_reg("mb", REGISTER, REG_MB);
    check_reg("ixh", REGISTER, REG_IXH);
    check_reg("ixl", REGISTER, REG_IXL);
    check_reg("iyh", REGISTER, REG_IYH);
    check_reg("iyl", REGISTER, REG_IYL);
    check_reg("z", FLAG, F_Z);
    check_reg("p", FLAG, F_P);
    check_reg("m", FLAG, F_M);
    check_reg("nz", FLAG, F_NZ);
    check_reg("nc", FLAG, F_NC);
    check_reg("po", FLAG, F_PO);
    check_reg("pe", FLAG, F_PE);

    /* Names that begin like a register but are not one must not be caught by
     * the shortcut: it decides on length as well as characters, and a
     * three-character name starting with "i" is the one place that is subtle. */
    {
        TOKEN tk = NONE;
        TK_TYPE tt = TY_NONE;
        const char* nots[] = {"ab", "ixz", "iz", "sq", "nx", "pq", "izl", "hlx", 0};
        int bad = 0;
        for (int i = 0; nots[i]; i++) {
            if (classify(nots[i], &tk, &tt) && (tk == REGISTER || tk == FLAG)) {
                fprintf(stderr, "FAIL  %s lexes as a register or flag\n", nots[i]);
                bad++;
            }
        }
        if (bad == 0) {
            fprintf(stderr, "PASS  %-44s ok\n", "near-miss names are not registers");
        } else {
            failures += bad;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
