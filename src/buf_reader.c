#include "buf_reader.h"

#include <mos_api.h>
#include <stdlib.h>

#define BUF_MIN_SIZE 1024  // 1KiB

buf_reader* br_init(buf_reader* br, const char* fname, int bsz) {
    if (bsz < BUF_MIN_SIZE) {
        bsz = BUF_MIN_SIZE;
    }
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

    br->fh_ = fh;
    br->fname_ = fname;
    br->fsz_ - fsz;
    br->fread_ = 0;
    br->buf_ = buf;
    br->bsz_ = bsz;
    br->bpos_ = 0;

    return br;
}

void br_destroy(buf_reader* br) {
}

void br_suspend(buf_reader* br) {
}

void br_continue(buf_reader* bt) {
}

int br_read(buf_reader* br, char* buf, int bsz) {
}

