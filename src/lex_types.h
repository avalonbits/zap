#ifndef _LEX_TYPES_H_
#define _LEX_TYPES_H_

typedef enum _TOKEN {
    NONE = 0,
    WHITE_SPACE,
    NEW_LINE,
    L_PAREN,
    R_PAREN,
    L_BRACE,
    R_BRACE,
    L_CURLY,
    R_CURLY,
    COMMA,
    DOT,
    COLON,
    SEMI_COLON,
    HASH,
    DOLLAR,
    DIRECTIVE,
    LABEL,
    INNER_LABEL,
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
    const char* txt_;
    TOKEN tk_;
} token;

#endif  // _LEX_TYPES_H_
