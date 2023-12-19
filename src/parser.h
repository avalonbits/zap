#ifndef _PARSER_H_
#define _PARSER_H_

#include "lexer.h"

typedef struct _parser {
    lexer lex_;

    int org_;
    bool adl_;
    bool skip_ws_;
    bool comment_;
} parser;

parser* parser_init(parser* p, const char* fname);
void parser_destroy(parser* p);

const char* parser_parse(parser* p, uint8_t buf, int sz);

#endif  // _PARSER_H_
