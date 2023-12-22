#include "lexer.h"

#include <agon/vdp_vdu.h>
#include <stdlib.h>

#include "hash_table.h"

static hash_table reserved;
static hash_table instructions;
static void init_ht() {
    hash_table* ht = &reserved;
    ht_init(ht, 64);
    ht_set(ht, "ADL", DIRECTIVE);
    ht_set(ht, "ALIGN", DIRECTIVE);
    ht_set(ht, "ASSUME", DIRECTIVE);
    ht_set(ht, "BLKB", DIRECTIVE);
    ht_set(ht, "BLKW", DIRECTIVE);
    ht_set(ht, "BLKP", DIRECTIVE);
    ht_set(ht, "BLKL", DIRECTIVE);
    ht_set(ht, "DB", DIRECTIVE);
    ht_set(ht, "DEFB", DIRECTIVE);
    ht_set(ht, "ASCII", DIRECTIVE);
    ht_set(ht, "BYTE", DIRECTIVE);
    ht_set(ht, "ASCIZ", DIRECTIVE);
    ht_set(ht, "DW", DIRECTIVE);
    ht_set(ht, "DEFW", DIRECTIVE);
    ht_set(ht, "DL", DIRECTIVE);
    ht_set(ht, "DW24", DIRECTIVE);
    ht_set(ht, "DW32", DIRECTIVE);
    ht_set(ht, "DS", DIRECTIVE);
    ht_set(ht, "DEFS", DIRECTIVE);
    ht_set(ht, "EQU", DIRECTIVE);
    ht_set(ht, "FILLBYTE", DIRECTIVE);
    ht_set(ht, "INCBIN", DIRECTIVE);
    ht_set(ht, "INCLUDE", DIRECTIVE);
    ht_set(ht, "MACRO", DIRECTIVE);
    ht_set(ht, "ENDMACRO", DIRECTIVE);
    ht_set(ht, "ORG", DIRECTIVE);
    ht_set(ht, "A", REG_A);
    ht_set(ht, "B", REG_B);
    ht_set(ht, "C", REG_C);
    ht_set(ht, "D", REG_D);
    ht_set(ht, "E", REG_E);
    ht_set(ht, "F", REG_F);
    ht_set(ht, "H", REG_H);
    ht_set(ht, "L", REG_L);
    ht_set(ht, "AF", REG_AF);
    ht_set(ht, "BC", REG_BC);
    ht_set(ht, "DE", REG_DE);
    ht_set(ht, "HL", REG_HL);
    ht_set(ht, "IX", REG_IX);
    ht_set(ht, "IY", REG_IY);

    ht = &instructions;
    ht_init(ht, 256);
    ht_set(ht, "ADC", INSTRUCTION);
    ht_set(ht, "ADD", INSTRUCTION);
    ht_set(ht, "AND", INSTRUCTION);
    ht_set(ht, "BIT", INSTRUCTION);
    ht_set(ht, "CALL", INSTRUCTION);
    ht_set(ht, "CCF", INSTRUCTION);
    ht_set(ht, "CP", INSTRUCTION);
    ht_set(ht, "CPD", INSTRUCTION);
    ht_set(ht, "CPDR", INSTRUCTION);
    ht_set(ht, "CPI", INSTRUCTION);
    ht_set(ht, "CPIR", INSTRUCTION);
    ht_set(ht, "CPL", INSTRUCTION);
    ht_set(ht, "DAA", INSTRUCTION);
    ht_set(ht, "DEC", INSTRUCTION);
    ht_set(ht, "DI", INSTRUCTION);
    ht_set(ht, "DJNZ", INSTRUCTION);
    ht_set(ht, "EI", INSTRUCTION);
    ht_set(ht, "EX", INSTRUCTION);
    ht_set(ht, "EXX", INSTRUCTION);
    ht_set(ht, "HALT", INSTRUCTION);
    ht_set(ht, "IM", INSTRUCTION);
    ht_set(ht, "IN", INSTRUCTION);
    ht_set(ht, "IN0", INSTRUCTION);
    ht_set(ht, "INC", INSTRUCTION);
    ht_set(ht, "IND", INSTRUCTION);
    ht_set(ht, "IND2", INSTRUCTION);
    ht_set(ht, "IND2R", INSTRUCTION);
    ht_set(ht, "INDM", INSTRUCTION);
    ht_set(ht, "INDMR", INSTRUCTION);
    ht_set(ht, "INDR", INSTRUCTION);
    ht_set(ht, "INDRX", INSTRUCTION);
    ht_set(ht, "INI", INSTRUCTION);
    ht_set(ht, "INI2", INSTRUCTION);
    ht_set(ht, "INI2R", INSTRUCTION);
    ht_set(ht, "INIM", INSTRUCTION);
    ht_set(ht, "INIMR", INSTRUCTION);
    ht_set(ht, "INIR", INSTRUCTION);
    ht_set(ht, "INIRX", INSTRUCTION);
    ht_set(ht, "JP", INSTRUCTION);
    ht_set(ht, "JR", INSTRUCTION);
    ht_set(ht, "LD", INSTRUCTION);
    ht_set(ht, "LDD", INSTRUCTION);
    ht_set(ht, "LDDR", INSTRUCTION);
    ht_set(ht, "LDI", INSTRUCTION);
    ht_set(ht, "LDIR", INSTRUCTION);
    ht_set(ht, "LEA", INSTRUCTION);
    ht_set(ht, "MLT", INSTRUCTION);
    ht_set(ht, "NEG", INSTRUCTION);
    ht_set(ht, "NOP", INSTRUCTION);
    ht_set(ht, "OR", INSTRUCTION);
    ht_set(ht, "OTD2R", INSTRUCTION);
    ht_set(ht, "OTDM", INSTRUCTION);
    ht_set(ht, "OTDMR", INSTRUCTION);
    ht_set(ht, "OTDR", INSTRUCTION);
    ht_set(ht, "OTDRX", INSTRUCTION);
    ht_set(ht, "OTI2R", INSTRUCTION);
    ht_set(ht, "OTIM", INSTRUCTION);
    ht_set(ht, "OTIMR", INSTRUCTION);
    ht_set(ht, "OTIR", INSTRUCTION);
    ht_set(ht, "OTIRX", INSTRUCTION);
    ht_set(ht, "OUT", INSTRUCTION);
    ht_set(ht, "OUT0", INSTRUCTION);
    ht_set(ht, "OUTD", INSTRUCTION);
    ht_set(ht, "OUTD2", INSTRUCTION);
    ht_set(ht, "OUTI", INSTRUCTION);
    ht_set(ht, "OUTI2", INSTRUCTION);
    ht_set(ht, "PEA", INSTRUCTION);
    ht_set(ht, "POP", INSTRUCTION);
    ht_set(ht, "PUSH", INSTRUCTION);
    ht_set(ht, "RES", INSTRUCTION);
    ht_set(ht, "RET", INSTRUCTION);
    ht_set(ht, "RETI", INSTRUCTION);
    ht_set(ht, "RETN", INSTRUCTION);
    ht_set(ht, "RL", INSTRUCTION);
    ht_set(ht, "RLA", INSTRUCTION);
    ht_set(ht, "RLC", INSTRUCTION);
    ht_set(ht, "RLCA", INSTRUCTION);
    ht_set(ht, "RLD", INSTRUCTION);
    ht_set(ht, "RR", INSTRUCTION);
    ht_set(ht, "RRA", INSTRUCTION);
    ht_set(ht, "RRC", INSTRUCTION);
    ht_set(ht, "RRCA", INSTRUCTION);
    ht_set(ht, "RRD", INSTRUCTION);
    ht_set(ht, "RSMIX", INSTRUCTION);
    ht_set(ht, "RST", INSTRUCTION);
    ht_set(ht, "SBC", INSTRUCTION);
    ht_set(ht, "SCF", INSTRUCTION);
    ht_set(ht, "SET", INSTRUCTION);
    ht_set(ht, "SLA", INSTRUCTION);
    ht_set(ht, "SLP", INSTRUCTION);
    ht_set(ht, "SRA", INSTRUCTION);
    ht_set(ht, "SRL", INSTRUCTION);
    ht_set(ht, "STMIX", INSTRUCTION);
    ht_set(ht, "SUB", INSTRUCTION);
    ht_set(ht, "TST", INSTRUCTION);
    ht_set(ht, "TSTIO", INSTRUCTION);
    ht_set(ht, "XOR", INSTRUCTION);
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
    free(lex);
}

static bool is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}

static bool is_digit(char ch) {
    return ch >= 48 && ch <= 57;
}

static bool is_ascdig(char ch) {
    return (ch >= 0x41 && ch <= 0x5A)
        || (ch >= 0x61 && ch <= 0x7A)
        || ch == '_' || is_digit(ch);
}

#define OK_CHAR(ch) (ch != EOF && ch != ESUSP)

token lex_next(lexer* lex) {
    token tk = {NULL, 0, NONE};
    char ch = br_char(&lex->rd_);
    if (!OK_CHAR(ch)) {
        return tk;
    }

    tk.txt_ = lex->line_;
    tk.txt_[0] = ch;
    tk.sz_ = 1;

    bool done = true;
    switch (ch) {
        case '=':
            tk.tk_ = EQUALS;
            break;
        case '+':
            tk.tk_ = PLUS;
            break;
        case '-':
            tk.tk_ = MINUS;
            done = false;
            break;
        case '\n':
            lex->lcount_++;
            tk.tk_ = NEW_LINE;
            break;
        case '\'':
            tk.tk_ = QUOTE;
            break;
        case '\"':
            tk.tk_ = D_QUOTE;
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
            done = false;
            break;
        default:
            done = false;
    }
    if (done) {
        return tk;
    }


    if (tk.tk_ == MINUS) {
        ch = br_peek(&lex->rd_);
        if (is_digit(ch)) {
            tk.tk_ = NUMBER;
            while (is_digit(ch)) {
                tk.txt_[tk.sz_++] = ch;
                br_next(&lex->rd_);
                ch = br_peek(&lex->rd_);
            }
        }
    } else if (tk.tk_ == DOLLAR) {
        ch = br_peek(&lex->rd_);
        if (is_digit(ch)) {
            tk.tk_ = HEX_NUMBER;
            tk.sz_ = 0;
            while (is_digit(ch)) {
                tk.txt_[tk.sz_++] = ch;
                br_next(&lex->rd_);
                ch = br_peek(&lex->rd_);
            }
        }
    } else if (is_space(ch)) {
        tk.tk_ = WHITE_SPACE;
        ch = br_peek(&lex->rd_);
        while (is_space(ch)) {
            tk.txt_[tk.sz_++] = ch;
            br_next(&lex->rd_);
            ch = br_peek(&lex->rd_);
        }
    } else if (is_digit(ch)) {
        tk.tk_ = NUMBER;
        ch = br_peek(&lex->rd_);
        while (is_digit(ch)) {
            tk.txt_[tk.sz_++] = ch;
            br_next(&lex->rd_);
            ch = br_peek(&lex->rd_);
        }
        if (ch == 'h' || ch == 'H') {
            tk.tk_ = HEX_NUMBER;
            br_next(&lex->rd_);
        }
    } else if (is_ascdig(ch)) {
        tk.tk_ = NAME;
        ch = br_peek(&lex->rd_);
        while (is_ascdig(ch)) {
            tk.txt_[tk.sz_++] = ch;
            br_next(&lex->rd_);
            ch = br_peek(&lex->rd_);
        }

        int val = ht_nget(&reserved, tk.txt_, tk.sz_, NULL);
        if (val == NONE) {
            val = ht_nget(&instructions, tk.txt_, tk.sz_, NULL);
        }
        if (val != NONE) {
            tk.tk_ = val;
        }
    }
    return tk;
}
