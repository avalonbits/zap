#include "lexer.h"

#include <stdlib.h>

#include "hash_table.h"

static hash_table directives;

static void ht_directives(hash_table* ht) {
    ht_init(ht, 32);
    ht_set(ht, "ADL", 255);
    ht_set(ht, "ALIGN", 255);
    ht_set(ht, "ASSUME", 255);
    ht_set(ht, "BLKB", 255);
    ht_set(ht, "BLKW", 255);
    ht_set(ht, "BLKP", 255);
    ht_set(ht, "BLKL", 255);
    ht_set(ht, "DB", 255);
    ht_set(ht, "DEFB", 255);
    ht_set(ht, "ASCII", 255);
    ht_set(ht, "BYTE", 255);
    ht_set(ht, "ASCIZ", 255);
    ht_set(ht, "DW", 255);
    ht_set(ht, "DEFW", 255);
    ht_set(ht, "DL", 255);
    ht_set(ht, "DW24", 255);
    ht_set(ht, "DW32", 255);
    ht_set(ht, "DS", 255);
    ht_set(ht, "DEFS", 255);
    ht_set(ht, "EQU", 255);
    ht_set(ht, "FILLBYTE", 255);
    ht_set(ht, "INCBIN", 255);
    ht_set(ht, "INCLUDE", 255);
    ht_set(ht, "MACRO", 255);
    ht_set(ht, "ENDMACRO", 255);
    ht_set(ht, "ORG", 255);
}

lexer* lex_init(const char* fname) {
    lexer* lx = (lexer*) malloc(sizeof(lexer));
    if (lx == NULL) {
        return NULL;
    }

    if (br_open(&lx->rd_, fname, 4) == NULL) {
        free(lx);
        return NULL;
    }

    lx->pos_ = 0;
    lx->sz_ = 0;
    lx->lcount_ = 1;

    ht_directives(&directives);

    return lx;
}

void lex_destroy(lexer* lex) {
    br_destroy(&lex->rd_);
    free(lex);
}

static bool is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}

static bool is_digit(char ch) {
    return ch >= 48 && ch <= 57;
}

token lex_next(lexer* lex) {
    token tk = {NULL, 0, NONE};
    if (lex->sz_ == EOF) {
        return tk;
    }

    const char* start = &lex->line_[lex->pos_];
    if (lex->pos_ == 0 && lex->sz_ == 0) {
        lex->sz_ = br_read(&lex->rd_, lex->line_, sizeof(lex->line_));
        if (lex->sz_ == EOF) {
            return tk;
        }
    }

    tk.txt_ = start;
    tk.sz_ = 1;
    for (; lex->pos_ < lex->sz_ && tk.tk_ == NONE; lex->pos_++) {
        char ch = lex->line_[lex->pos_];
        switch (ch) {
             case '=':
                tk.tk_ = EQUALS;
                break;
            case '\n':
                lex->lcount_++;
                tk.tk_ = NEW_LINE;
                break;
            case '(':
                tk.tk_ = L_PAREN;
                break;
            case ')':
                tk.tk_ = R_PAREN;
                break;
            case ',':
                tk.tk_ = COMMA;
                break;
            case '.':
                tk.tk_ = DOT;
                break;
            case ':':
                tk.tk_ = COLON;
                break;
            case ';':
                tk.tk_ = SEMI_COLON;
                break;
            case '#':
                tk.tk_= HASH;
                break;
            case '$':
                tk.tk_ = DOLLAR;
                break;
            default:
                if (is_space(ch)) {
                    tk.tk_ = WHITE_SPACE;
                    while (is_space(ch)) {
                        ch = lex->line_[++lex->pos_];
                        if (lex->pos_ == lex->sz_) {
                            lex->pos_ = 0;
                            lex->sz_ = 0;
                            return tk;
                        }
                    }
                } else if (is_digit(ch)) {
                    tk.sz_ = 0;
                    tk.tk_ = NUMBER;
                    while (is_digit(ch)) {
                        tk.sz_++;
                        ch = lex->line_[++lex->pos_];
                        if (lex->pos_ == lex->sz_) {
                            lex->pos_ = 0;
                            lex->sz_ =0;
                            return tk;
                        }
                    }
                } else {
                    tk.sz_ = 0;
                    tk.tk_ = LABEL;
                    while (!is_space(ch)) {
                        tk.sz_++;
                        ch = lex->line_[++lex->pos_];
                        if (lex->pos_ == lex->sz_) {
                            lex->pos_ = 0;
                            lex->sz_ = 0;
                            return tk;
                        }
                    }
                    if (ht_nget(&directives, tk.txt_, tk.sz_, NULL) == 255) {
                        tk.tk_ = DIRECTIVE;
                    }
                }
               lex->pos_--;
                break;
        }
    }
    if (lex->pos_ == lex->sz_) {
        lex->pos_ = 0;
        lex->sz_ = 0;
    }
    return tk;
}
