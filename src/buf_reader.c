#include "buf_reader.h"

#include <agon/mos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_MIN_SIZE 1024 // 1KiB

buf_reader* br_open(buf_reader* br, const char* fname, int bsz_kb) {
    int bsz = bsz_kb <= 0 ? BUF_MIN_SIZE : bsz_kb << 10;
    const uint8_t fh = mos_fopen(fname, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
    if (fh == 0) {
        return NULL;
    }

    char* buf = (char*) malloc(bsz * sizeof(char));
    if (buf == NULL) {
        mos_fclose(fh);
        return NULL;
    }

    FIL* fil = mos_getfil(fh);
    if (fil == NULL) {
        free(buf);
        mos_fclose(fh);
        return NULL;
    }

    const int fsz = (int) fil->obj.objsize;
    if (fsz < 0) {
        free(buf);
        mos_fclose(fh);

        return NULL;
    }

    /* An empty file is a valid source that assembles to nothing, which is what
     * the reference does with one. It used to be refused as if it could not be
     * opened. */
    uint24_t read = mos_fread(fh, buf, bsz);

    br->fh_ = fh;
    br->fname_ = fname;
    br->fsz_ = fsz;
    br->fread_ = 0;
    br->buf_ = buf;
    br->bsz_ = read;
    br->bpos_ = 0;
    br->mem_ = false;
    br->owned_ = true;

    return br;
}

buf_reader* br_open_mem(buf_reader* br, const char* text, int len) {
    char* buf = (char*) malloc((len > 0 ? len : 1) * sizeof(char));
    if (buf == NULL) {
        return NULL;
    }
    for (int i = 0; i < len; i++) {
        buf[i] = text[i];
    }

    br->fh_ = 0;
    br->fname_ = NULL;
    br->fsz_ = len;
    br->fread_ = 0;
    br->buf_ = buf;
    br->bsz_ = (uint24_t) len;
    br->bpos_ = 0;
    br->mem_ = true;
    br->owned_ = true;

    return br;
}

void br_close(buf_reader* br) {
    br_suspend(br);
    free(br->buf_);
    /* The inline fast path relies on buf_ never being NULL while bsz_ is
     * non-zero. */
    br->buf_ = NULL;
    br->bsz_ = 0;
    br->bpos_ = 0;
}

void br_destroy(buf_reader* br) {
    br->fname_ = 0;
    if (br->fh_ != 0) {
        mos_fclose(br->fh_);
    }
    br->fh_ = 0;
    if (br->buf_ != NULL) {
        free(br->buf_);
        br->buf_ = NULL;
    }
    br->bsz_ = 0;
    br->bpos_ = 0;
    br->fsz_ = 0;
    br->fread_ = 0;
}

bool br_suspend(buf_reader* br) {
    if (br->bsz_ == 0) {
        return EOF;
    }

    if (br->buf_ == NULL || br->fh_ == 0) {
        return false;
    }
    if (br->fh_ != 0) {
        mos_fclose(br->fh_);
    }
    br->fh_ = 0;

    return true;
}

bool br_resume(buf_reader* br) {
    if (br->bsz_ == 0) {
        return false;
    }

    if (br->buf_ == NULL || br->fh_ != 0) {
        return false;
    }

    const uint8_t fh = mos_fopen(br->fname_, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
    if (fh == 0) {
        return false;
    }
    br->fh_ = fh;
    mos_flseek(fh, br->fread_);
    return true;
}

/* Everything br_peek needs when the buffer is exhausted. The inline fast path
 * in the header handles the case where it is not. */
char br_fill_peek(buf_reader* br) {
    if (br->bsz_ == 0) {
        return EOF;
    }
    if (br->buf_ == NULL) {
        return ESUSP;
    }
    if (br->mem_) {
        /* No refill: the buffer is the whole content. */
        return EOF;
    }
    if (br->fh_ == 0) {
        return ESUSP;
    }
    if (br->bpos_ == br->bsz_) {
        uint24_t frsz = mos_fread(br->fh_, br->buf_, br->bsz_);
        if (frsz == 0) {
            br->bsz_ = 0;
            return EOF;
        }
        br->bpos_ = 0;
        br->bsz_ = frsz;
    }
    return br->buf_[br->bpos_];
}

int br_byte(buf_reader* br) {
    if (br->bsz_ == 0) {
        return -1;
    }
    if (br->buf_ == NULL) {
        return -1;
    }
    if (br->mem_) {
        return br->bpos_ == br->bsz_ ? -1
             : (int) (unsigned char) br->buf_[br->bpos_++];
    }
    if (br->fh_ == 0) {
        return -1;
    }
    if (br->bpos_ == br->bsz_) {
        uint24_t frsz = mos_fread(br->fh_, br->buf_, br->bsz_);
        if (frsz == 0) {
            br->bsz_ = 0;

            return -1;
        }
        br->bpos_ = 0;
        br->bsz_ = frsz;
    }

    return (int) (unsigned char) br->buf_[br->bpos_++];
}

/* The out-of-line forms, kept for callers outside the lexer. */
char br_peek(buf_reader* br) {
    return br_peek_inline(br);
}

void br_next(buf_reader* br) {
    br_next_inline(br);
}

char br_char(buf_reader* br) {
    return br_char_inline(br);
}
