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
} buf_reader;

buf_reader* br_open(buf_reader* br, const char* fname, int bsz);
void br_close(buf_reader* br);
void br_destroy(buf_reader* br);
bool br_suspend(buf_reader* br);
bool br_resume(buf_reader* br);

char br_char(buf_reader* br);
char br_peek(buf_reader* br);
void br_next(buf_reader* br);

#endif // _BUF_READER_H_
