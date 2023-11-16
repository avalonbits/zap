#ifndef _LEXER_H_
#define _LEXER_H_

#include <stdint.h>

typedef struct _lexer  {
    uint8_t fh_;
    char* buf_;
    int bsz_;
} lexer;


lexer* lex_init(const char* fname);

#endif  // _LEXER_H_
