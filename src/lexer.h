#ifndef _LEXER_H_
#define _LEXER_H_

#include <stdint.h>

#include "buf_reader.h"

typedef struct _lexer  {
    buf_reader rd_;
    char line[512];
} lexer;


lexer* lex_init(const char* fname);
void lex_destroy(lexer* lex);

#endif  // _LEXER_H_
