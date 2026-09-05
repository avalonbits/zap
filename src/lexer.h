#ifndef _LEXER_H_
#define _LEXER_H_

#include <stdint.h>

#include "buf_reader.h"
#include "lex_types.h"

/* Size of a lexer's source buffer, in KiB.
 *
 * A sweep on the Agon found the gain flat to 8 KiB and real at 16, where the
 * cost is MOS read calls rather than instructions -- on the host the whole
 * range is within 0.04%, and past 16 it is a slight loss on sources made of
 * small files.
 *
 * It cannot simply be raised: every source suspended by an .include keeps its
 * buffer, so the parser's include stack holds MAX_INCLUDE_DEPTH + 1 of these
 * at once. At 64 KiB that is 576 KiB, more than an Agon Light has in total.
 * test_lexer.c pins the budget. */
#define LEX_BUF_KB 16

typedef struct _lexer  {
    buf_reader rd_;
    int lcount_;

    /* Which source this is, for diagnostics. A program with a dozen includes
     * reports a line number that means nothing without it. */
    char fname_[64];
} lexer;


lexer* lex_init(lexer* lex, const char* fname);

/* Builds the reserved-word tables. lex_init does this itself; a lexer set up
 * over memory has to ask for it. */
void lex_prime(void);
void lex_destroy(lexer* lex);

/* Reads the next token into *out.
 *
 * Written through a pointer rather than returned: a token is 13 bytes on the
 * eZ80, and returning one by value is a block copy on every call -- 473,762 of
 * them on the benchmark source. Shrinking that same copy by four bytes was
 * worth 3.4%, so removing it outright is the same trade taken further. */
void lex_next(lexer* lex, token* out);

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

/* Copies raw source text up to, but not including, the line whose first word
 * is `stop`. Used to capture a macro body: it has to be kept as text, because
 * expansion substitutes into it before it is lexed. Returns the length, or -1
 * if `stop` is never found, -2 if it does not fit. The terminating line is
 * consumed. */
int lex_capture(lexer* lex, const char* stop, char* out, int max);

#endif  // _LEXER_H_
