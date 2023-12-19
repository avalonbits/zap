#include "parser.h"

#include <mos_api.h>

#include "lexer.h"

static char errmsg[256] = "";

parser* parser_init(parser* p, const char* fname) {
    if (lex_init(&p->lex_, fname) == NULL) {
        return NULL;
    }

    p->org_ = 0x400000;
    p->adl_ = true;
    p->skip_ws_ = true;
    p->comment_ = false;

    return p;
}

void parser_destroy(parser* p) {
    lex_destroy(&p->lex_);
}

const char* parser_parse(parser* p, uint8_t buf, int sz) {
    lexer* l = &p->lex_;
    for (token tk = lex_next(l); tk.tk_ != NONE; tk = lex_next(l)) {
        print_token(tk);
        if (tk.tk_ == NEW_LINE) {
            mos_puts("NL\r\n", 0, 0);
        } else {
            putch(' ');
        }
    }
    return errmsg;
}

