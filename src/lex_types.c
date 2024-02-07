#include "lex_types.h"

#include <agon/vdp_vdu.h>

int pack_tktt(TOKEN tk, TK_TYPE tt) {
    return (((int)tt) << 12) | ((int)tk);
}

TOKEN unpack_tk(int v) {
    return (TOKEN) (v  & 0xFFF);
}

TK_TYPE unpack_tt(int v) {
    return (TK_TYPE) ((v >> 12) & 0xFFF);
}

void print_token(token tk) {
    switch (tk.tk_) {
        case NEW_LINE:
            mos_puts("NL\r\n", 0, 0);
            break;
        case WHITE_SPACE:
            mos_puts(" WS ", 0, 0);
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
        case B_SLASH:
            putch('\\');
            break;
        case F_SLASH:
            putch('/');
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
            mos_puts("R(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        case FLAG:
            mos_puts("F(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        default:
            mos_puts("UN(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
    }
}
