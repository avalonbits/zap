#ifndef _LEXER_H_
#define _LEXER_H_

#include <stdint.h>

#include "buf_reader.h"
#include "lex_types.h"

typedef struct _lexer  {
    buf_reader rd_;
    char line_[256];
    int lcount_;
} lexer;


lexer* lex_init(lexer* lex, const char* fname);
void lex_destroy(lexer* lex);

token lex_next(lexer* lex);

/* Reads the body of a double-quoted string, the opening quote already
 * consumed, resolving backslash escapes. Returns the number of bytes, or -1 if
 * the string is unterminated, -2 on an escape that does not exist, or -3 if it
 * does not fit.
 *
 * This works on characters rather than tokens on purpose. A string read
 * through the token stream is at the mercy of what its characters happen to
 * lex as -- an apostrophe in it starts a character literal, so
 * db "\'" could not be written at all. */
int lex_string(lexer* lex, char* out, int max);

#endif  // _LEXER_H_
