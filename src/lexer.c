#include "lexer.h"

#include <agon/mos.h>
#include <stdbool.h>
#include <stdlib.h>

#include "hash_table.h"
#include "isa.h"
#include "value.h"

static hash_table reserved;
static hash_table instructions;

/* The reserved-word and mnemonic tables are immutable and shared by every
 * lexer, so they are built once. Rebuilding them per lex_init -- which is what
 * used to happen -- leaked both tables on every open. */
static bool ht_ready = false;

static void init_ht() {
    if (ht_ready) {
        return;
    }
    ht_ready = true;

    hash_table* ht = &reserved;
    ht_init(ht, 128);
    ht_set(ht, "ADL", pack_tktt(DIRECTIVE, D_ADL));
    ht_set(ht, "ALIGN", pack_tktt(DIRECTIVE, D_ALIGN));
    ht_set(ht, "ASSUME", pack_tktt(DIRECTIVE, D_ASSUME));
    ht_set(ht, "BLKB", pack_tktt(DIRECTIVE, D_BLKB));
    ht_set(ht, "BLKW", pack_tktt(DIRECTIVE, D_BLKW));
    ht_set(ht, "BLKP", pack_tktt(DIRECTIVE, D_BLKP));
    ht_set(ht, "BLKL", pack_tktt(DIRECTIVE, D_BLKL));
    ht_set(ht, "DB", pack_tktt(DIRECTIVE, D_DB));
    ht_set(ht, "DEFB", pack_tktt(DIRECTIVE, D_DEFB));
    ht_set(ht, "ASCII", pack_tktt(DIRECTIVE, D_ASCII));
    ht_set(ht, "BYTE", pack_tktt(DIRECTIVE, D_BYTE));
    ht_set(ht, "ASCIZ", pack_tktt(DIRECTIVE, D_ASCIZ));
    ht_set(ht, "DW", pack_tktt(DIRECTIVE, D_DW));
    ht_set(ht, "DEFW", pack_tktt(DIRECTIVE, D_DEFW));
    ht_set(ht, "DL", pack_tktt(DIRECTIVE, D_DL));
    ht_set(ht, "DW24", pack_tktt(DIRECTIVE, D_DW24));
    ht_set(ht, "DW32", pack_tktt(DIRECTIVE, D_DW32));
    ht_set(ht, "DS", pack_tktt(DIRECTIVE, D_DS));
    ht_set(ht, "DEFS", pack_tktt(DIRECTIVE, D_DEFS));
    ht_set(ht, "EQU", pack_tktt(DIRECTIVE, D_EQU));
    ht_set(ht, "FILLBYTE", pack_tktt(DIRECTIVE, D_FILLBYTE));
    ht_set(ht, "INCBIN", pack_tktt(DIRECTIVE, D_INCBIN));
    ht_set(ht, "INCLUDE", pack_tktt(DIRECTIVE, D_INCLUDE));
    ht_set(ht, "MACRO", pack_tktt(DIRECTIVE, D_MACRO));
    ht_set(ht, "ENDMACRO", pack_tktt(DIRECTIVE, D_ENDMACRO));
    ht_set(ht, "ORG", pack_tktt(DIRECTIVE, D_ORG));
    ht_set(ht, "CPU", pack_tktt(DIRECTIVE, D_CPU));
    ht_set(ht, "A", pack_tktt(REGISTER, REG_A));
    ht_set(ht, "B", pack_tktt(REGISTER, REG_B));
    ht_set(ht, "C", pack_tktt(REGISTER, REG_C));
    ht_set(ht, "D", pack_tktt(REGISTER, REG_D));
    ht_set(ht, "E", pack_tktt(REGISTER, REG_E));
    ht_set(ht, "F", pack_tktt(REGISTER, REG_F));
    ht_set(ht, "H", pack_tktt(REGISTER, REG_H));
    ht_set(ht, "L", pack_tktt(REGISTER, REG_L));
    ht_set(ht, "AF", pack_tktt(REGISTER, REG_AF));
    ht_set(ht, "BC", pack_tktt(REGISTER, REG_BC));
    ht_set(ht, "DE", pack_tktt(REGISTER, REG_DE));
    ht_set(ht, "HL", pack_tktt(REGISTER, REG_HL));
    ht_set(ht, "IX", pack_tktt(REGISTER, REG_IX));
    ht_set(ht, "IY", pack_tktt(REGISTER, REG_IY));
    ht_set(ht, "SP", pack_tktt(REGISTER, REG_SP));
    ht_set(ht, "IXH", pack_tktt(REGISTER, REG_IXH));
    ht_set(ht, "IXL", pack_tktt(REGISTER, REG_IXL));
    ht_set(ht, "IYH", pack_tktt(REGISTER, REG_IYH));
    ht_set(ht, "IYL", pack_tktt(REGISTER, REG_IYL));
    ht_set(ht, "I", pack_tktt(REGISTER, REG_I));
    ht_set(ht, "MB", pack_tktt(REGISTER, REG_MB));
    ht_set(ht, "R", pack_tktt(REGISTER, REG_RR));
    ht_set(ht, "NZ", pack_tktt(FLAG, F_NZ));
    ht_set(ht, "Z", pack_tktt(FLAG, F_Z));
    ht_set(ht, "NC", pack_tktt(FLAG, F_NC));
    ht_set(ht, "PO", pack_tktt(FLAG, F_PO));
    ht_set(ht, "PE", pack_tktt(FLAG, F_PE));
    ht_set(ht, "P", pack_tktt(FLAG, F_P));
    ht_set(ht, "M", pack_tktt(FLAG, F_M));

    ht = &instructions;
    ht_init(ht, 255);

    /* Built from the generated instruction table rather than a list kept by
     * hand, so a mnemonic can never be known to the lexer and missing from the
     * encoder, or the other way round. The token's type is the row index. */
    for (int i = 0; i < isa_table_count; i++) {
        ht_set(ht, isa_table[i].name, pack_tktt(INSTRUCTION, (TK_TYPE) i));
    }
}

lexer* lex_init(lexer* lex, const char* fname) {
    if (br_open(&lex->rd_, fname, 4) == NULL) {
        return NULL;
    }

    lex->lcount_ = 1;

    init_ht();

    return lex;
}

void lex_destroy(lexer* lex) {
    br_destroy(&lex->rd_);
    /* The lexer itself is not freed here. Every caller embeds it -- pr_destroy
     * passes &p->lex_, where p is usually a stack variable -- so freeing the
     * argument was handing the allocator an interior pointer, and every
     * successful assembly ended in heap corruption. */
}

static bool is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}

static bool is_digit(char ch) {
    return ch >= 48 && ch <= 57;
}

static bool is_hex_digit(char ch) {
    return (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || is_digit(ch);
}

static bool is_ascdig(char ch) {
    return (ch >= 0x41 && ch <= 0x5A)
        || (ch >= 0x61 && ch <= 0x7A)
        || ch == '_' || ch == '@' || is_digit(ch);
}

#define OK_CHAR(ch) (ch != EOF && ch != ESUSP)

/* Appends to the token text, refusing to run off the end. The buffer used to
 * be written without a bound, so a name longer than the line buffer wrote past
 * the lexer struct. An over-long token is truncated here and will fail to
 * resolve as a name or a literal, which is the right outcome for input that
 * cannot be legal anyway. */
static void push_ch(lexer* lex, token* tk, char ch) {
    if (tk->sz_ < (int) sizeof(lex->line_) - 1) {
        tk->txt_[tk->sz_++] = ch;
    }
}

/* Reads the body of a character literal, the opening quote already consumed.
 *
 * The awkward case is a backslash followed by a quote. '\'' is an escaped
 * quote and '\' is a literal backslash, and the two are told apart only by
 * whether another quote follows -- which is how the reference assembler reads
 * them, and its corpus pins both. */
static void lex_char_literal(lexer* lex, token* tk) {
    tk->tk_ = BAD_LITERAL;

    char ch = br_peek(&lex->rd_);
    if (!OK_CHAR(ch) || ch == '\n') {
        return;  /* nothing after the opening quote */
    }
    push_ch(lex, tk, ch);
    br_next(&lex->rd_);

    if (ch == '\'') {
        return;  /* the empty literal '' -- both quotes consumed */
    }

    char val;
    if (ch == '\\') {
        const char esc = br_peek(&lex->rd_);
        if (!OK_CHAR(esc) || esc == '\n') {
            return;
        }
        push_ch(lex, tk, esc);
        br_next(&lex->rd_);

        if (esc == '\'' && br_peek(&lex->rd_) != '\'') {
            /* '\' -- the quote just consumed was the closing one, so the
             * backslash stands for itself. */
            tk->tk_ = NUMBER;
            tk->val_ = 0x5C;

            return;
        }
        if (!esc_char(esc, &val)) {
            return;
        }
    } else {
        val = ch;
    }

    if (br_peek(&lex->rd_) != '\'') {
        return;  /* more than one character, or never closed */
    }
    push_ch(lex, tk, '\'');
    br_next(&lex->rd_);

    tk->tk_ = NUMBER;
    tk->val_ = (value) (unsigned char) val;
}

/* Reads a $, # or % prefixed literal, the prefix already consumed and stored.
 * A bare $ is the program counter rather than a literal, so it stays a
 * DOLLAR for the expression evaluator to resolve. */
static void lex_prefixed_number(lexer* lex, token* tk, TOKEN bare) {
    char ch = br_peek(&lex->rd_);
    while (OK_CHAR(ch) && is_hex_digit(ch)) {
        push_ch(lex, tk, ch);
        br_next(&lex->rd_);
        ch = br_peek(&lex->rd_);
    }

    if (tk->sz_ > 1 && num_parse(tk->txt_, tk->sz_, &tk->val_)) {
        tk->tk_ = NUMBER;

        return;
    }
    tk->tk_ = bare;
}

token lex_next(lexer* lex) {
    token tk = {NULL, 0, NONE, TY_NONE, 0, false};
    char ch = br_char(&lex->rd_);
    if (!OK_CHAR(ch)) {
        return tk;
    }

    tk.txt_ = lex->line_;
    tk.tk_ = UNKNOWN;
    push_ch(lex, &tk, ch);

    switch (ch) {
        case '=':  tk.tk_ = EQUALS;      return tk;
        case '+':  tk.tk_ = PLUS;        return tk;
        case '*':  tk.tk_ = STAR;        return tk;
        case '/':  tk.tk_ = F_SLASH;     return tk;
        case '&':  tk.tk_ = AMPERSAND;   return tk;
        case '|':  tk.tk_ = PIPE;        return tk;
        case '^':  tk.tk_ = CARET;       return tk;
        case '~':  tk.tk_ = TILDE;       return tk;
        case '"':  tk.tk_ = D_QUOTE;     return tk;
        case '\\': tk.tk_ = B_SLASH;     return tk;
        case '(':  tk.tk_ = L_PAREN;     return tk;
        case ')':  tk.tk_ = R_PAREN;     return tk;
        case '[':  tk.tk_ = L_BRACKET;   return tk;
        case ']':  tk.tk_ = R_BRACKET;   return tk;
        case ',':  tk.tk_ = COMMA;       return tk;
        case '.':  tk.tk_ = DOT;         return tk;
        case ':':  tk.tk_ = COLON;       return tk;
        case ';':  tk.tk_ = SEMI_COLON;  return tk;

        case '\n':
            lex->lcount_++;
            tk.tk_ = NEW_LINE;

            return tk;

        // A minus is always an operator now. It used to swallow the digits
        // after it, which made -5 a literal and left no way to write label-2
        // or $-2 at all.
        case '-':
            tk.tk_ = MINUS;

            return tk;

        case '\'':
            lex_char_literal(lex, &tk);

            return tk;

        case '$':
            lex_prefixed_number(lex, &tk, DOLLAR);

            return tk;

        case '#':
            lex_prefixed_number(lex, &tk, HASH);

            return tk;

        case '%':
            lex_prefixed_number(lex, &tk, UNKNOWN);

            return tk;

        case '<':
        case '>':
            if (br_peek(&lex->rd_) == ch) {
                push_ch(lex, &tk, ch);
                br_next(&lex->rd_);
                tk.tk_ = (ch == '<') ? SHIFT_L : SHIFT_R;
            }

            return tk;

        default:
            break;
    }

    if (is_space(ch)) {
        tk.tk_ = WHITE_SPACE;
        ch = br_peek(&lex->rd_);
        while (OK_CHAR(ch) && is_space(ch)) {
            push_ch(lex, &tk, ch);
            br_next(&lex->rd_);
            ch = br_peek(&lex->rd_);
        }

        return tk;
    }

    if (!is_ascdig(ch)) {
        return tk;
    }

    ch = br_peek(&lex->rd_);
    while (OK_CHAR(ch) && is_ascdig(ch)) {
        push_ch(lex, &tk, ch);
        br_next(&lex->rd_);
        ch = br_peek(&lex->rd_);
    }

    // A literal is claimed before a name, so Ah and 0b1h are numbers rather
    // than identifiers. That also means an identifier can never shadow a
    // literal, which is how the reference resolves the same ambiguity.
    if (num_parse(tk.txt_, tk.sz_, &tk.val_)) {
        tk.tk_ = NUMBER;

        return tk;
    }

    tk.tk_ = NAME;

    /* An identifier with a colon straight after it is a label being defined,
     * so it keeps its own name rather than being resolved against the
     * reserved words. That is what lets a routine be called pea: or nz:,
     * which the reference allows and which its Labels corpus depends on. The
     * colon has to be immediate -- "lbl :" is not a label there either. */
    if (br_peek(&lex->rd_) == ':') {
        tk.label_ = true;

        return tk;
    }

    int val = ht_nget(&reserved, tk.txt_, tk.sz_, NULL);
    if (unpack_tk(val) == NONE) {
        val = ht_nget(&instructions, tk.txt_, tk.sz_, NULL);
    }
    if (unpack_tk(val) != NONE) {
        tk.tk_ = unpack_tk(val);
        tk.tt_ = unpack_tt(val);
    }

    return tk;
}
