#ifndef _LEX_TYPES_H_
#define _LEX_TYPES_H_

typedef enum _TOKEN {
    NONE = 0,
    WHITE_SPACE,
    NEW_LINE,
    EQUALS,
    PLUS,
    MINUS,
    QUOTE,
    D_QUOTE,
    L_PAREN,
    R_PAREN,
    COMMA,
    DOT,
    COLON,
    SEMI_COLON,
    HASH,
    DOLLAR,
    DIRECTIVE,
    NAME,
    NUMBER,
    HEX_NUMBER,

    INSTRUCTION,
    REG_A,
    REG_B,
    REG_C,
    REG_D,
    REG_E,
    REG_F,
    REG_H,
    REG_L,
    REG_AF,
    REG_BC,
    REG_DE,
    REG_HL,
    REG_IX,
    REG_IY,
} TOKEN;


typedef struct _token {
    char* txt_;
    int sz_;
    TOKEN tk_;
} token;

void print_token(token tk);


#endif  // _LEX_TYPES_H_
