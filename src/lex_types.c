#include "lex_types.h"

#include <agon/vdp_vdu.h>

void print_token(token tk) {
    switch (tk.tk_) {
        case NEW_LINE:
            break;
        case WHITE_SPACE:
            mos_puts("WS", 0, 0);
            break;
        case NUMBER:
            mos_puts("NUMBER(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        case EQUALS:
            putch('=');
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
        case LABEL:
            mos_puts("LABEL(", 0, 0);
            mos_puts(tk.txt_, tk.sz_, 0);
            putch(')');
            break;
        default:
            mos_puts("SOMETHING", 0, 0);
            break;
    }
}
