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

char br_char(buf_reader* br);

/* Reads one byte as 0..255, or -1 at end of file.
 *
 * br_char cannot be used on binary data: it returns a char, and a data byte of
 * 0xFF is indistinguishable from its EOF sentinel, so an .incbin stopped at
 * the first 0xFF in the file. */
int br_byte(buf_reader* br);
char br_peek(buf_reader* br);
void br_next(buf_reader* br);

#endif // _BUF_READER_H_
