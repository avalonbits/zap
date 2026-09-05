#include "lexer.h"

#include <agon/mos.h>
#include <stdbool.h>
#include <stdlib.h>

#include <string.h>

#include "hash_table.h"

#include "isa.h"
#include "value.h"

/* Reserved words and mnemonics live in one table: the two sets share no name,
 * and keeping them apart cost every identifier two lookups instead of one. */
static hash_table words;

/* The reserved-word and mnemonic tables are immutable and shared by every
 * lexer, so they are built once. Rebuilding them per lex_init -- which is what
 * used to happen -- leaked both tables on every open. */
static bool ht_ready = false;

static void init_ht() {
    if (ht_ready) {
        return;
    }
    ht_ready = true;

    hash_table* ht = &words;
    ht_init(ht, 256, true);
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
    ht_set(ht, "RELOCATE", pack_tktt(DIRECTIVE, D_RELOCATE));
    ht_set(ht, "ENDRELOCATE", pack_tktt(DIRECTIVE, D_ENDRELOCATE));
    ht_set(ht, "IF", pack_tktt(DIRECTIVE, D_IF));
    ht_set(ht, "ELSE", pack_tktt(DIRECTIVE, D_ELSE));
    ht_set(ht, "ENDIF", pack_tktt(DIRECTIVE, D_ENDIF));
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


    /* Built from the generated instruction table rather than a list kept by
     * hand, so a mnemonic can never be known to the lexer and missing from the
     * encoder, or the other way round. The token's type is the row index. */
    for (int i = 0; i < isa_table_count; i++) {
        ht_set(ht, isa_table[i].name, pack_tktt(INSTRUCTION, (TK_TYPE) i));
    }
}

void lex_prime(void) {
    init_ht();
}

lexer* lex_init(lexer* lex, const char* fname) {
    if (br_open(&lex->rd_, fname, LEX_BUF_KB) == NULL) {
        return NULL;
    }

    lex->lcount_ = 1;

    int n = 0;
    while (fname[n] != 0 && n < (int) sizeof(lex->fname_) - 1) {
        lex->fname_[n] = fname[n];
        n++;
    }
    lex->fname_[n] = 0;

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

/* Register and flag names, matched directly rather than looked up.
 *
 * There are 29 of them and none is longer than three characters, and together
 * they are a third of every identifier the lexer classifies -- 8,744 of 26,400
 * on BBC BASIC. Hashing a one-character name to discover it is the register B
 * costs more than deciding it here: this is a compare and a jump, with no
 * table to load and nothing to miss in a cache the eZ80 does not have.
 *
 * Taking precedence over the reserved-word table is safe only because no
 * mnemonic shares a name with a register or a flag. That is checked rather
 * than assumed -- see the test, which walks the generated instruction table
 * looking for one. If a future eZ80 revision adds a mnemonic called "P", the
 * table would have overridden the flag and this would not, so the test fails
 * rather than the assembler quietly changing meaning.
 *
 * Returns a packed token/type as the table would, or 0 for anything else. */
static int reg_or_flag(const char* t, int sz) {
    /* Folded here rather than through lower() per character: at most three
     * characters, and the lexer's own case-insensitivity is what this has to
     * reproduce. */
    const char a = (char) (t[0] | 0x20);

    if (sz == 1) {
        switch (a) {
            case 'a': return pack_tktt(REGISTER, REG_A);
            case 'b': return pack_tktt(REGISTER, REG_B);
            case 'c': return pack_tktt(REGISTER, REG_C);
            case 'd': return pack_tktt(REGISTER, REG_D);
            case 'e': return pack_tktt(REGISTER, REG_E);
            case 'f': return pack_tktt(REGISTER, REG_F);
            case 'h': return pack_tktt(REGISTER, REG_H);
            case 'l': return pack_tktt(REGISTER, REG_L);
            case 'i': return pack_tktt(REGISTER, REG_I);
            case 'r': return pack_tktt(REGISTER, REG_RR);
            case 'z': return pack_tktt(FLAG, F_Z);
            case 'p': return pack_tktt(FLAG, F_P);
            case 'm': return pack_tktt(FLAG, F_M);
            default:  return 0;
        }
    }

    if (sz == 2) {
        const char b = (char) (t[1] | 0x20);
        switch (a) {
            case 'a': return b == 'f' ? pack_tktt(REGISTER, REG_AF) : 0;
            case 'b': return b == 'c' ? pack_tktt(REGISTER, REG_BC) : 0;
            case 'd': return b == 'e' ? pack_tktt(REGISTER, REG_DE) : 0;
            case 'h': return b == 'l' ? pack_tktt(REGISTER, REG_HL) : 0;
            case 's': return b == 'p' ? pack_tktt(REGISTER, REG_SP) : 0;
            case 'm': return b == 'b' ? pack_tktt(REGISTER, REG_MB) : 0;
            case 'i':
                if (b == 'x') return pack_tktt(REGISTER, REG_IX);
                if (b == 'y') return pack_tktt(REGISTER, REG_IY);

                return 0;
            case 'n':
                if (b == 'z') return pack_tktt(FLAG, F_NZ);
                if (b == 'c') return pack_tktt(FLAG, F_NC);

                return 0;
            case 'p':
                if (b == 'o') return pack_tktt(FLAG, F_PO);
                if (b == 'e') return pack_tktt(FLAG, F_PE);

                return 0;
            default: return 0;
        }
    }

    /* Three characters, and only the index halves reach here. */
    if (sz == 3 && a == 'i') {
        const char b = (char) (t[1] | 0x20);
        const char c = (char) (t[2] | 0x20);
        if (b == 'x') {
            if (c == 'h') return pack_tktt(REGISTER, REG_IXH);
            if (c == 'l') return pack_tktt(REGISTER, REG_IXL);
        } else if (b == 'y') {
            if (c == 'h') return pack_tktt(REGISTER, REG_IYH);
            if (c == 'l') return pack_tktt(REGISTER, REG_IYL);
        }
    }

    return 0;
}

static bool is_mnemonic(const char* txt, int sz) {
    return unpack_tk(ht_nget(&words, txt, (uint8_t) sz, NULL)) == INSTRUCTION;
}

/* Characters come straight out of the reader's buffer.
 *
 * None of these refills. The buffer holds whole lines, so the only point at
 * which more input can be needed is the start of a token, and lex_next asks
 * there. That takes the refill test off the path every character used to pay
 * for. */
static inline char l_peek(const lexer* lex) {
    const buf_reader* r = &lex->rd_;

    return r->bpos_ < r->bsz_ ? r->buf_[r->bpos_] : (char) EOF;
}

static inline void l_next(lexer* lex) {
    lex->rd_.bpos_++;
}

static inline char l_char(lexer* lex) {
    buf_reader* r = &lex->rd_;

    return r->bpos_ < r->bsz_ ? r->buf_[r->bpos_++] : (char) EOF;
}

/* Runs over a stretch of characters without going back to the reader for each.
 *
 * l_peek and l_char reach through the lexer for buf_, bpos_ and bsz_ on every
 * character: a load of the base, a load of the position, a load of the limit,
 * a compare, an indexed load, and a store back. That is the right shape for
 * deciding what a token is, which happens once, and the wrong one for scanning
 * the body of a name, which happens for every character of every identifier in
 * the source.
 *
 * These hold the three in locals for the length of a run and write the
 * position back once at the end. The compiler cannot do it: buf_reader is
 * reachable through a pointer, so it has to assume anything might change it.
 *
 * Safe because none of these refills. The buffer holds whole lines, so the
 * only point at which more input can be needed is the start of a token, and
 * lex_next asks there -- a run cannot reach the end of the buffer without
 * reaching the newline that ends its line first. The bound is kept anyway, so
 * a buffer that somehow did end mid-line stops the run rather than running off
 * it. */
typedef struct _scan {
    const char* base;
    uint24_t pos;
    uint24_t end;
} scan;

static inline scan scan_open(const lexer* lex) {
    scan sc;
    sc.base = lex->rd_.buf_;
    sc.pos = lex->rd_.bpos_;
    sc.end = lex->rd_.bsz_;

    return sc;
}

/* Writes the position back and extends the token by what the run covered. */
static inline char scan_close(lexer* lex, token* tk, const scan* sc) {
    tk->sz_ += (int) (sc->pos - lex->rd_.bpos_);
    lex->rd_.bpos_ = sc->pos;

    return sc->pos < sc->end ? sc->base[sc->pos] : (char) EOF;
}

/* Extends the token by the character at the scan position.
 *
 * There is nothing to copy: a token's text is a pointer into the buffer, so it
 * grows by counting one more character of what it already points at. That also
 * retires the bound the copy needed -- a token cannot outrun its line, and a
 * line has to fit the buffer to be read at all. */
static inline void take_ch(lexer* lex, token* tk) {
    lex->rd_.bpos_++;
    tk->sz_++;
}


/* Reads the body of a character literal, the opening quote already consumed.
 *
 * The awkward case is a backslash followed by a quote. '\'' is an escaped
 * quote and '\' is a literal backslash, and the two are told apart only by
 * whether another quote follows -- which is how the reference assembler reads
 * them, and its corpus pins both. */
static void lex_char_literal(lexer* lex, token* tk) {
    tk->tk_ = BAD_LITERAL;

    char ch = l_peek(lex);
    if (!OK_CHAR(ch) || ch == '\n') {
        return;  /* nothing after the opening quote */
    }
    take_ch(lex, tk);

    if (ch == '\'') {
        return;  /* the empty literal '' -- both quotes consumed */
    }

    char val;
    if (ch == '\\') {
        const char esc = l_peek(lex);
        if (!OK_CHAR(esc) || esc == '\n') {
            return;
        }
        take_ch(lex, tk);

        if (esc == '\'' && l_peek(lex) != '\'') {
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

    if (l_peek(lex) != '\'') {
        return;  /* more than one character, or never closed */
    }
    take_ch(lex, tk);

    tk->tk_ = NUMBER;
    tk->val_ = (value) (unsigned char) val;
}

/* Reads a $, # or % prefixed literal, the prefix already consumed and stored.
 * A bare $ is the program counter rather than a literal, so it stays a
 * DOLLAR for the expression evaluator to resolve. */
static void lex_prefixed_number(lexer* lex, token* tk, TOKEN bare) {
    char ch = l_peek(lex);
    while (OK_CHAR(ch) && is_hex_digit(ch)) {
        take_ch(lex, tk);
        ch = l_peek(lex);
    }

    if (tk->sz_ > 1 && num_parse(tk->txt_, tk->sz_, &tk->val_)) {
        tk->tk_ = NUMBER;

        return;
    }
    tk->tk_ = bare;
}

int lex_string(lexer* lex, char* out, int max) {
    int n = 0;

    while (true) {
        const char ch = l_peek(lex);
        if (!OK_CHAR(ch) || ch == '\n') {
            return -1;
        }
        l_next(lex);

        if (ch == '"') {
            return n;
        }

        char put = ch;
        if (ch == '\\') {
            const char esc = l_peek(lex);
            if (!OK_CHAR(esc) || esc == '\n') {
                return -1;
            }
            l_next(lex);
            if (!esc_char(esc, &put)) {
                return -2;
            }
        }

        if (n >= max) {
            return -3;
        }
        out[n++] = put;
    }
}

int lex_capture(lexer* lex, const char* stop, char* out, int max) {
    int n = 0;

    while (true) {
        const int start = n;

        /* Refill through the same path the scanner uses. Reading raw here --
         * which is what this did -- refills without trimming to a newline, so
         * a macro body crossing a buffer boundary left the buffer ending in
         * the middle of a line. The scanner does not check for a refill on
         * every character, so the next token was silently truncated at that
         * point: seven placements in a sweep of forty-one either failed or,
         * worse, assembled to different bytes. */
        if (lex->rd_.bpos_ >= lex->rd_.bsz_) {
            bool too_long = false;
            if (!br_fill_lines(&lex->rd_, &too_long)) {
                return too_long ? -2 : -1;
            }
        }

        /* One whole line. The buffer never ends mid-line, so the newline that
         * ends this one is in it, unless this is the last line of the file. */
        const char* line = &lex->rd_.buf_[lex->rd_.bpos_];
        const uint24_t avail = lex->rd_.bsz_ - lex->rd_.bpos_;
        uint24_t len = 0;
        while (len < avail && line[len] != '\n') {
            len++;
        }
        if (len < avail) {
            len++;   /* take the newline with it */
            lex->lcount_++;
        }

        if (n + (int) len > max) {
            return -2;
        }
        memcpy(&out[n], line, (size_t) len);
        n += (int) len;
        lex->rd_.bpos_ += len;

        if (n == start) {
            return -1;  /* end of file without the terminator */
        }

        /* Does this line begin with the terminator? */
        int i = start;
        while (i < n && (out[i] == ' ' || out[i] == '\t' || out[i] == '\r')) {
            i++;
        }
        /* The dot on a directive is optional, so the terminator may be
         * written either "endmacro" or ".endmacro". */
        if (i < n && out[i] == '.') {
            i++;
        }
        int w = 0;
        while (i + w < n && is_ascdig(out[i + w])) {
            w++;
        }

        int sn = 0;
        while (stop[sn] != 0) {
            sn++;
        }
        if (w == sn) {
            bool match = true;
            for (int k = 0; k < w; k++) {
                char a = out[i + k];
                char b = stop[k];
                if (a >= 'A' && a <= 'Z') a = (char) (a + 0x20);
                if (b >= 'A' && b <= 'Z') b = (char) (b + 0x20);
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return start;  /* drop the terminating line */
            }
        }

    }
}

void lex_next(lexer* lex, token* out) {
    token* const tkp = out;
    tkp->txt_ = NULL;
    tkp->sz_ = 0;
    tkp->tk_ = NONE;
    tkp->tt_ = TY_NONE;
    tkp->val_ = 0;
    tkp->label_ = false;

    /* The one place more input can be needed. A token never spans a refill,
     * because the buffer ends on a newline and no token contains one. */
    if (lex->rd_.bpos_ >= lex->rd_.bsz_) {
        bool too_long = false;
        if (!br_fill_lines(&lex->rd_, &too_long)) {
            if (too_long) {
                lex->lcount_++;
                tkp->tk_ = LINE_TOO_LONG;
            }

            return;
        }
    }

    /* Whitespace between tokens is skipped here rather than turned into a
     * token for the caller to throw away. Nothing has wanted one since strings
     * started being read character by character. */
    scan gap = scan_open(lex);
    while (gap.pos < gap.end && is_space(gap.base[gap.pos])) {
        gap.pos++;
    }
    if (gap.pos >= gap.end) {
        lex->rd_.bpos_ = gap.pos;

        return;
    }
    const char ch0 = gap.base[gap.pos];
    gap.pos++;
    lex->rd_.bpos_ = gap.pos;
    char ch = ch0;

    /* The token's text is a run of the buffer beginning at the character just
     * consumed. Nothing is copied: it grows by counting. */
    tkp->txt_ = &lex->rd_.buf_[lex->rd_.bpos_ - 1];
    tkp->sz_ = 1;
    tkp->tk_ = UNKNOWN;

    switch (ch) {
        case '=':  tkp->tk_ = EQUALS;      return;
        case '+':  tkp->tk_ = PLUS;        return;
        case '*':  tkp->tk_ = STAR;        return;
        case '/':  tkp->tk_ = F_SLASH;     return;
        case '&':  tkp->tk_ = AMPERSAND;   return;
        case '|':  tkp->tk_ = PIPE;        return;
        case '^':  tkp->tk_ = CARET;       return;
        case '~':  tkp->tk_ = TILDE;       return;
        case '"':  tkp->tk_ = D_QUOTE;     return;
        case '\\': tkp->tk_ = B_SLASH;     return;
        case '(':  tkp->tk_ = L_PAREN;     return;
        case ')':  tkp->tk_ = R_PAREN;     return;
        case '[':  tkp->tk_ = L_BRACKET;   return;
        case ']':  tkp->tk_ = R_BRACKET;   return;
        case ',':  tkp->tk_ = COMMA;       return;
        case '.':  tkp->tk_ = DOT;         return;
        case ':':  tkp->tk_ = COLON;       return;
        case ';': {
            /* A comment runs to the end of the line and none of it means
             * anything, so it is consumed here rather than handed to the
             * parser a token at a time. Every word in a comment used to pay
             * for a scan, a Pearson hash and a reserved-table lookup before
             * being discarded.
             *
             * What comes back is the newline, not the semicolon. A comment
             * ends the statement and nothing else, which is all the parser
             * ever did with the semicolon: it set a flag, dropped every token
             * until the newline, and returned that. Emitting the newline
             * directly is the same token stream with one less round trip and
             * no flag to keep. */
            /* The token does not grow over a comment, so the position is
             * written back directly rather than through scan_close. */
            scan sc = scan_open(lex);
            while (sc.pos < sc.end && sc.base[sc.pos] != '\n') {
                sc.pos++;
            }
            const bool had_nl = sc.pos < sc.end;
            if (had_nl) {
                sc.pos++;   /* and the newline that ends it */
            }
            lex->rd_.bpos_ = sc.pos;
            lex->lcount_++;

            /* Retype the token. It began at the semicolon, but what it
             * stands for is the newline -- the character just consumed. A file
             * that ends without one leaves nothing to point at, and the token
             * carries no text. */
            if (had_nl) {
                tkp->txt_ = &lex->rd_.buf_[lex->rd_.bpos_ - 1];
                tkp->sz_ = 1;
            } else {
                tkp->sz_ = 0;
            }
            tkp->tk_ = NEW_LINE;

            return;
        }

        case '\n':
            lex->lcount_++;
            tkp->tk_ = NEW_LINE;

            return;

        // A minus is always an operator now. It used to swallow the digits
        // after it, which made -5 a literal and left no way to write label-2
        // or $-2 at all.
        case '-':
            tkp->tk_ = MINUS;

            return;

        case '\'':
            lex_char_literal(lex, tkp);

            return;

        case '$':
            lex_prefixed_number(lex, tkp, DOLLAR);

            return;

        case '#':
            lex_prefixed_number(lex, tkp, HASH);

            return;

        case '%':
            lex_prefixed_number(lex, tkp, UNKNOWN);

            return;

        case '<':
        case '>':
            if (l_peek(lex) == ch) {
                take_ch(lex, tkp);
                tkp->tk_ = (ch == '<') ? SHIFT_L : SHIFT_R;
            }

            return;

        default:
            break;
    }

    if (!is_ascdig(ch)) {
        return;
    }

    {
        scan sc = scan_open(lex);
        while (sc.pos < sc.end && is_ascdig(sc.base[sc.pos])) {
            sc.pos++;
        }
        ch = scan_close(lex, tkp, &sc);
    }

    /* A dot continues a name unless what has been read so far is a mnemonic,
     * in which case it introduces a suffix instead. That is the only thing
     * separating "rst.lil" from a label called "FFOBJID.fs", and real sources
     * have both. */
    while (ch == '.' && !is_mnemonic(tkp->txt_, tkp->sz_)) {
        take_ch(lex, tkp);
        scan sc = scan_open(lex);
        while (sc.pos < sc.end && is_ascdig(sc.base[sc.pos])) {
            sc.pos++;
        }
        ch = scan_close(lex, tkp, &sc);
    }

    /* Registers and flags are decided before the literal test rather than
     * after it.
     *
     * num_parse runs on every identifier-shaped token to claim things like Ah
     * and 0b1h as numbers, and its cheap reject lets every one-character token
     * through -- so a, b, c and d each reached scan_base before being
     * recognised as registers. On a source dense in registers that was 6% of
     * all work spent proving that "a" is not a decimal number.
     *
     * Safe because no register or flag name parses as a number, which the test
     * asserts for all 29 of them in both cases rather than leaving it to the
     * eye: the single-character forms are forced to decimal so a..f are not
     * hex digits, and of the longer ones only mb, ixh and iyh even reach
     * scan_base, where they fail on their first character. If that ever stops
     * being true the two orders disagree and the test says so. */
    const int reg = reg_or_flag(tkp->txt_, tkp->sz_);
    if (reg != 0) {
        tkp->tk_ = unpack_tk(reg);
        tkp->tt_ = unpack_tt(reg);

        return;
    }

    // A literal is claimed before a name, so Ah and 0b1h are numbers rather
    // than identifiers. That also means an identifier can never shadow a
    // literal, which is how the reference resolves the same ambiguity.
    if (num_parse(tkp->txt_, tkp->sz_, &tkp->val_)) {
        tkp->tk_ = NUMBER;
        /* Flagged so the parser can tell "5:" -- someone naming a label after
         * a number -- from a bare literal, and say which it is. */
        tkp->label_ = l_peek(lex) == ':';

        return;
    }

    tkp->tk_ = NAME;

    /* An identifier with a colon straight after it is a label being defined,
     * so it keeps its own name rather than being resolved against the
     * reserved words. That is what lets a routine be called pea: or nz:,
     * which the reference allows and which its Labels corpus depends on. The
     * colon has to be immediate -- "lbl :" is not a label there either. */
    if (l_peek(lex) == ':') {
        tkp->label_ = true;

        return;
    }

    const int val = ht_nget(&words, tkp->txt_, tkp->sz_, NULL);
    if (unpack_tk(val) != NONE) {
        tkp->tk_ = unpack_tk(val);
        tkp->tt_ = unpack_tt(val);
    }

    return;
}
