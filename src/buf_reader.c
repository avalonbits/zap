#include "buf_reader.h"

#include <mos_api.h>
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
    if (fsz <= 0) {
        free(buf);
        mos_fclose(fh);
        return NULL;
    }

    uint24_t read = mos_fread(fh, buf, bsz);
    if (read == 0) {
        free(buf);
        mos_fclose(fh);
        return NULL;
    }

    br->fh_ = fh;
    br->fname_ = fname;
    br->fsz_ = fsz;
    br->fread_ = 0;
    br->buf_ = buf;
    br->bsz_ = read;
    br->bpos_ = 0;

    return br;
}

void br_close(buf_reader* br) {
    br_suspend(br);
    free(br->buf_);
}

void br_destroy(buf_reader* br) {
    br->fname_ = 0;
    mos_fclose(br->fh_);
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

char br_peek(buf_reader* br) {
    if (br->bsz_ == 0) {
        return EOF;
    }
    if (br->buf_ == NULL || br->fh_ == 0) {
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

void br_next(buf_reader* br) {
    if (br->bsz_ == 0) {
        return;
    }
    if (br->buf_ == NULL || br->fh_ == 0) {
        return;
    }
    if (br->bpos_ <= br->bsz_) {
        br->bpos_++;
    }
}

char br_char(buf_reader* br) {
    char ch = br_peek(br);
    if (ch != EOF && ch != ESUSP) {
        br->bpos_++;
    }
    return ch;
}
