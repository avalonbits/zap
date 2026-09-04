#ifndef _LEX_TYPES_H_
#define _LEX_TYPES_H_

#include "value.h"

typedef enum _TOKEN {
    NONE = 0,
    UNKNOWN,
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
    B_SLASH,
    F_SLASH,

    // Expression operators. F_SLASH doubles as divide, and MINUS and PLUS are
    // both binary and unary.
    STAR,
    AMPERSAND,
    PIPE,
    CARET,
    TILDE,
    SHIFT_L,
    SHIFT_R,

    // Brackets group an expression. Parentheses cannot: they already mean
    // indirect addressing.
    L_BRACKET,
    R_BRACKET,

    NAME,

    // Every numeric literal -- decimal, hex, binary, character -- arrives as a
    // NUMBER carrying its converted value in val_. HEX_NUMBER is gone: nothing
    // downstream cared which base it was written in, and keeping the two apart
    // meant every use site re-parsed the text.
    NUMBER,

    // A character literal that is empty, unterminated, holds more than one
    // character, or uses an escape that does not exist.
    BAD_LITERAL,
    DIRECTIVE,
    INSTRUCTION,
    REGISTER,
    FLAG,
} TOKEN;

typedef enum _TK_TYPE {
    TY_NONE = -1,
    D_ADL = 0,
    D_ALIGN,
    D_ASSUME,
    D_BLKB,
    D_BLKW,
    D_BLKP,
    D_BLKL,
    D_DB,
    D_DEFB,
    D_ASCII,
    D_BYTE,
    D_ASCIZ,
    D_DW,
    D_DEFW,
    D_DL,
    D_DW24,
    D_DW32,
    D_DS,
    D_DEFS,
    D_EQU,
    D_FILLBYTE,
    D_INCBIN,
    D_INCLUDE,
    D_MACRO,
    D_ENDMACRO,
    D_ORG,
    D_CPU,
    D_RELOCATE,
    D_ENDRELOCATE,

    // We group the single letter registers like this in order to make it
    // easier to generate the instructions involving them.
    REG_B,
    REG_C,
    REG_D,
    REG_E,
    REG_H,
    REG_L,
    REG_F,
    REG_A,

    REG_AF,
    REG_BC,
    REG_DE,
    REG_HL,
    REG_IX,
    REG_IY,
    REG_SP,

    // The halves of IX and IY, and the three special registers. Undocumented
    // on the Z80, documented on the eZ80, and the reference accepts them.
    REG_IXH,
    REG_IXL,
    REG_IYH,
    REG_IYL,
    REG_I,
    REG_MB,
    REG_RR,   // the R register; REG_R would read as a general-purpose one

    F_NZ,
    F_Z,
    F_NC,
    // F_C,  This would colide with REG_C
    F_PO,
    F_PE,
    F_P,
    F_M,

    ISA_ADC,
    ISA_ADD,
    ISA_AND,
    ISA_BIT,
    ISA_CALL,
    ISA_CCF,
    ISA_CP,
    ISA_CPD,
    ISA_CPDR,
    ISA_CPI,
    ISA_CPIR,
    ISA_CPL,
    ISA_DAA,
    ISA_DEC,
    ISA_DI,
    ISA_DJNZ,
    ISA_EI,
    ISA_EX,
    ISA_EXX,
    ISA_HALT,

    ISA_IM,
    ISA_IN,
    ISA_IN0,
    ISA_INC,
    ISA_IND,
    ISA_IND2,
    ISA_IND2R,
    ISA_INDM,
    ISA_INDMR,
    ISA_INDR,
    ISA_INDRX,
    ISA_INI,
    ISA_INI2,
    ISA_INI2R,
    ISA_INIM,
    ISA_INIMR,
    ISA_INIR,
    ISA_INIRX,
    ISA_JP,
    ISA_JR,

    ISA_LD,
    ISA_LDD,
    ISA_LDDR,
    ISA_LDI,
    ISA_LDIR,
    ISA_LEA,
    ISA_MLT,
    ISA_NEG,
    ISA_NOP,
    ISA_OR,
    ISA_OTD2R,
    ISA_OTDM,
    ISA_OTDMR,
    ISA_OTDR,
    ISA_OTDRX,
    ISA_OTI2R,
    ISA_OTIM,
    ISA_OTIMR,
    ISA_OTIR,
    ISA_OTIRX,

    ISA_OUT,
    ISA_OUT0,
    ISA_OUTD,
    ISA_OUTD2,
    ISA_OUTI,
    ISA_OUTI2,
    ISA_PEA,
    ISA_POP,
    ISA_PUSH,
    ISA_RES,
    ISA_RET,
    ISA_RETI,
    ISA_RETN,
    ISA_RL,
    ISA_RLA,
    ISA_RLC,
    ISA_RLCA,
    ISA_RLD,
    ISA_RR,
    ISA_RRA,

    ISA_RRC,
    ISA_RRCA,
    ISA_RRD,
    ISA_RSMIX,
    ISA_RST,
    ISA_SBC,
    ISA_SCF,
    ISA_SET,
    ISA_SLA,
    ISA_SLP,
    ISA_SRA,
    ISA_SRL,
    ISA_STMIX,
    ISA_SUB,
    ISA_TST,
    ISA_TSTIO,
    ISA_XOR

} TK_TYPE;

typedef struct _token {
    char* txt_;
    int sz_;
    TOKEN tk_;
    TK_TYPE tt_;

    // Set for NUMBER. Carried on the token so that no site has to convert the
    // text a second time.
    value val_;

    // Set when a colon followed the name with nothing in between, which is
    // what makes it a label being defined. "lbl :" is not a label, in zap or
    // in the reference.
    bool label_;
} token;

int pack_tktt(TOKEN tk, TK_TYPE tt);
TOKEN unpack_tk(int v);
TK_TYPE unpack_tt(int v);

void print_token(token tk);


#endif  // _LEX_TYPES_H_
