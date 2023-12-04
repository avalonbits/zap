#ifndef _LEXER_H_
#define _LEXER_H_

#include <stdint.h>

#include "buf_reader.h"
#include "lex_types.h"

typedef struct _lexer  {
    buf_reader rd_;
    char line_[256];
    uint8_t pos_;
    int sz_;
    int lcount_;
} lexer;


lexer* lex_init(const char* fname);
void lex_destroy(lexer* lex);

token lex_next(lexer* lex);

#endif  // _LEXER_H_
