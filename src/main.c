#include <stdio.h>
#include <mos_api.h>

#include "lexer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        mos_puts("\r\nUsage: zap <filename>\r\n", 0, 0);
        return 0;
    }

    lexer* lex = lex_init(argv[1]);
    if (lex == NULL) {
        return -1;
    }
    int i = 0;
    for (token tk = lex_next(lex); i < 100 && tk.tk_ != NONE; i++, tk = lex_next(lex)) {
        if (tk.tk_ == NONE) {
            break;
        }

        print_token(tk);
        if (tk.tk_ == NEW_LINE) {
            mos_puts("\r\n", 0, 0);
        } else {
            putch(' ');
        }
    }
    lex_destroy(lex);

    return 0;
}
