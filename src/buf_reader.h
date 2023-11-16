#ifndef _BUF_READER_H_
#define _BUF_READER_H_

#include <stdint.h>

#define EOF -1

typedef struct _buf_reader  {
    uint8_t fh_;
    const char* fname_;
    int fsz_;
    int fread_;

    char* buf_;
    int bsz_;
    int bpos_;
} buf_reader;

buf_reader* br_init(buf_reader* br, const char* fname, int bsz);
void br_destroy(buf_reader* br);
void br_suspend(buf_reader* br);
void br_continue(buf_reader* bt);

int br_read(buf_reader* br, char* buf, int bsz);

#endif // _BUF_READER_H_
