#include "lex_types.h"

#include <agon/vdp_vdu.h>

int pack_tktt(TOKEN tk, TK_TYPE tt) {
    // TK_TYPE is already shifted 12 bits, so this works as expected.
    return ((int)tt) | ((int)tk);
}

TOKEN unpack_tk(int v) {
    return (TOKEN) (v & 0xFFF);
}

TK_TYPE unpack_tt(int v) {
    return (TK_TYPE) ((v >> 12) & 0xFFF);
}

void print_token(token tk) {
    switch (tk.tk_) {
        case NEW_LINE:
            mos_puts("NL", 0, 0);
            break;
        case WHITE_SPACE:
            mos_puts("WS", 0, 0);
            break;
        case NUMBER:
        case HEX_NUMBER:
            mos_puts("NUMBER(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        case EQUALS:
            putch('=');
            break;
        case PLUS:
            putch('+');
            break;
        case MINUS:
            putch('-');
            break;
        case QUOTE:
            putch('\'');
            break;
        case D_QUOTE:
            putch('"');
            break;
        case L_PAREN:
            putch('(');
            break;
        case R_PAREN:
            putch(')');
            break;
        case COMMA:
            putch(',');
            break;
        case DOT:
            putch('.');
            break;
        case COLON:
            putch(':');
            break;
        case SEMI_COLON:
            putch(';');
            break;
        case HASH:
            putch('#');
            break;
        case DOLLAR:
            putch('$');
            break;
        case NAME:
            mos_puts("N(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        case DIRECTIVE:
            mos_puts("D(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        case INSTRUCTION:
            mos_puts("I(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        case REGISTER:
            mos_puts("REGISTER(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        default:
            mos_puts("SOMETHING", 0, 0);
            break;
    }
}
