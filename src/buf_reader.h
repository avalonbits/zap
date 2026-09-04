#ifndef _BUF_READER_H_
#define _BUF_READER_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef EOF
#define EOF   -1
#endif

#define ESUSP -2


typedef struct _buf_reader  {
    uint8_t fh_;
    const char* fname_;
    int fsz_;
    uint24_t fread_;

    char* buf_;

    /* cap_ is how much the buffer holds, raw_ how much was read into it, and
     * bsz_ how much of that the reader hands out. bsz_ used to be all three,
     * so a short read shrank the buffer permanently.
     *
     * For a source file they differ on purpose: a read is trimmed back to the
     * last newline in it, so bsz_ always ends a line, and the bytes between
     * bsz_ and raw_ are the start of the next one, carried to the front of the
     * buffer on the next refill. A token can then point straight into buf_ --
     * it cannot span a refill, because a refill only happens at a line end. */
    uint24_t cap_;
    uint24_t raw_;
    uint24_t bsz_;
    uint24_t bpos_;

    /* Reading from memory rather than a file: there is no handle and no
     * refill, the buffer is the whole content. Macro expansion needs it, and
     * so does assembling an editor's buffer without writing it out first. */
    bool mem_;
    bool owned_;   /* whether the buffer has to be freed */
} buf_reader;

buf_reader* br_open(buf_reader* br, const char* fname, int bsz);

/* Reads from a copy of the given text. The copy is freed by br_destroy. */
buf_reader* br_open_mem(buf_reader* br, const char* text, int len);
void br_close(buf_reader* br);
void br_destroy(buf_reader* br);
bool br_suspend(buf_reader* br);
bool br_resume(buf_reader* br);

/* Slow paths: end of buffer, refill, suspended. */
char br_fill_peek(buf_reader* br);

/* Peeks at the next byte without consuming it.
 *
 * The common case -- a byte already in the buffer -- is inline and a single
 * comparison. It is called once or twice per source character, which on the
 * eZ80 made the call itself a measurable share of lexing, and the five checks
 * behind it were paid on every one. buf_ is only ever NULL when bsz_ is zero,
 * so the fast path cannot dereference it. */
static inline char br_peek_inline(buf_reader* br) {
    if (br->bpos_ < br->bsz_) {
        return br->buf_[br->bpos_];
    }

    return br_fill_peek(br);
}

static inline void br_next_inline(buf_reader* br) {
    if (br->bpos_ < br->bsz_) {
        br->bpos_++;
    }
}

static inline char br_char_inline(buf_reader* br) {
    if (br->bpos_ < br->bsz_) {
        return br->buf_[br->bpos_++];
    }

    const char ch = br_fill_peek(br);
    if (ch != EOF && ch != ESUSP) {
        br->bpos_++;
    }

    return ch;
}

char br_char(buf_reader* br);

/* Reads one byte as 0..255, or -1 at end of file.
 *
 * br_char cannot be used on binary data: it returns a char, and a data byte of
 * 0xFF is indistinguishable from its EOF sentinel, so an .incbin stopped at
 * the first 0xFF in the file. */
int br_byte(buf_reader* br);

/* Consumes and hands back a run of buffered bytes at once. Returns the count,
 * or 0 at end of file; *out points into the reader's buffer and is valid
 * until the next read. Used by .incbin, which copies whole files. */
int br_block(buf_reader* br, const char** out);

/* Refills so the buffer holds whole lines: reads, then trims back to the last
 * newline, carrying the partial line after it to the front next time. Returns
 * false at end of file; *too_long is set if one line does not fit the buffer.
 *
 * This is the lexer's refill. Binary data has no newlines, so .incbin keeps
 * using br_block, which refills without trimming. */
bool br_fill_lines(buf_reader* br, bool* too_long);
char br_peek(buf_reader* br);
void br_next(buf_reader* br);

#endif // _BUF_READER_H_
