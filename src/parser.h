#ifndef _PARSER_H_
#define _PARSER_H_

#include <stdint.h>

#include "hash_table.h"
#include "lexer.h"


typedef struct _parser {
    lexer lex_;
    uint8_t* buf_;
    int sz_;
    int pos_;

    int org_;
    bool adl_;
    bool skip_ws_;
    bool comment_;

    token tk_;

    hash_table labels_;
} parser;

parser* pr_init(parser* p, const char* fname);
void pr_destroy(parser* p);

const char* pr_parse(parser* p);

#endif  // _PARSER_H_
