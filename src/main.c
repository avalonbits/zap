#include <stdio.h>
#include <mos_api.h>

#include "lexer.h"

int main(int argc, char** argv) {
    if (argc == 1) {
        mos_puts("\r\nUsage: zap <filename>\r\n", 0, 0);
        return 0;
    }

    lexer* lex = lex_init(argv[1]);
    if (lex == NULL) {
        return -1;
    }
    for (token tk = lex_next(lex); tk.tk_ != NONE; tk = lex_next(lex)) {
    }
    lex_destroy(lex);

    return 0;
}
