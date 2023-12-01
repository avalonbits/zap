#include <stdio.h>
#include <mos_api.h>

#include "lexer.h"

int main(int argc, char** argv) {
    if (argc == 1) {
        mos_puts("usage: zap <filename>", 0, 0);
        return 0;
    }

    lexer* lex = lex_init(argv[1]);
    if (lex == NULL) {
        return -1;
    }
    lex_destroy(lex);

    return 0;
}
