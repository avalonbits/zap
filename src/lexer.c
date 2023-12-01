#include "lexer.h"

#include <stdlib.h>

lexer* lex_init(const char* fname) {
    lexer* lx = (lexer*) malloc(sizeof(lexer));
    if (lx == NULL) {
        return NULL;
    }

    if (br_open(&lx->rd_, fname, 4) == NULL) {
        free(lx);
        return NULL;
    }

    return lx;
}

void lex_destroy(lexer* lex) {
    br_destroy(&lex->rd_);
    free(lex);
}
