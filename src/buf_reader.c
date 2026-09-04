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

    /* Nothing is read here. The first read is a refill like any other, so a
     * source reader's very first buffer ends on a newline the same way every
     * later one does, instead of being a case of its own.
     *
     * An empty file is a valid source that assembles to nothing, which is what
     * the reference does with one. It used to be refused as if it could not be
     * opened. */

    br->fh_ = fh;
    br->fname_ = fname;
    br->fsz_ = fsz;
    br->fread_ = 0;
    br->buf_ = buf;
    br->cap_ = (uint24_t) bsz;
    br->raw_ = 0;
    br->bsz_ = 0;
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
    br->cap_ = (uint24_t) len;
    br->raw_ = (uint24_t) len;
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
    br->cap_ = 0;
    br->raw_ = 0;
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
    br->cap_ = 0;
    br->raw_ = 0;
    br->bsz_ = 0;
    br->bpos_ = 0;
    br->fsz_ = 0;
    br->fread_ = 0;
}

bool br_suspend(buf_reader* br) {
    if (br->cap_ == 0) {
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
    if (br->cap_ == 0) {
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
    if (br->cap_ == 0) {
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
        uint24_t frsz = mos_fread(br->fh_, br->buf_, br->cap_);
        if (frsz == 0) {
            br->cap_ = 0;
    br->raw_ = 0;
    br->bsz_ = 0;
            return EOF;
        }
        br->bpos_ = 0;
        br->raw_ = frsz;
        br->bsz_ = frsz;
    }
    return br->buf_[br->bpos_];
}

/* Hands back a run of bytes the reader already holds, and consumes them.
 * Returns how many are available at *out, or 0 at end of file. The pointer is
 * valid only until the next read from this reader.
 *
 * .incbin copies whole files into the output, and doing that a byte at a time
 * cost a call in here and a call into the output writer for every one of
 * them. Every condition that makes br_byte return -1 makes this return 0, so
 * the two agree on where a file ends -- including a suspended reader, which
 * both treat as end of file. */
/* Refills so the buffer ends on a newline.
 *
 * Reads into whatever the last refill could not use, then walks back from the
 * end for the last newline and hands out only as far as that. What follows it
 * is the beginning of a line that is not all here yet; it moves to the front
 * on the next call rather than being re-read, which would cost a seek and a
 * second read of the same bytes.
 *
 * The invariant it buys is that a line is never split across a refill, so the
 * lexer has no seam to handle and a token can point into the buffer. It also
 * subsumes the line length check: a line that does not fit the buffer is the
 * only way the backward walk can fail. */
bool br_fill_lines(buf_reader* br, bool* too_long) {
    *too_long = false;

    if (br->cap_ == 0 || br->buf_ == NULL) {
        return false;
    }

    if (br->mem_) {
        /* The whole content is already there; there is nothing to refill. */
        return false;
    }
    if (br->fh_ == 0) {
        return false;
    }

    /* Carry the partial line the last read ended on. */
    const uint24_t carry = br->raw_ - br->bsz_;
    if (carry > 0) {
        memmove(br->buf_, &br->buf_[br->bsz_], (size_t) carry);
    }

    const uint24_t frsz = mos_fread(br->fh_, &br->buf_[carry], br->cap_ - carry);
    br->raw_ = carry + frsz;
    br->bpos_ = 0;

    if (br->raw_ == 0) {
        br->bsz_ = 0;

        return false;
    }

    if (frsz < br->cap_ - carry) {
        /* A read shorter than asked for is the end of the file. Everything
         * left is the last line, which often has no newline and is a line
         * regardless -- so there is nothing to trim back to. Testing only for
         * a read of zero missed this: a file whose final read returns its last
         * few bytes has no newline to find, and looked like a line too long. */
        br->bsz_ = br->raw_;

        return true;
    }

    for (uint24_t i = br->raw_; i > 0; i--) {
        if (br->buf_[i - 1] == '\n') {
            br->bsz_ = i;

            return true;
        }
    }

    /* No newline anywhere in a full buffer: one line is longer than the
     * buffer, and no further reading can change that. The reader is spent, so
     * that a caller which keeps asking gets end of file rather than the same
     * complaint forever -- the prescan does exactly that, and span in place
     * until this was terminal. */
    br->cap_ = 0;
    br->raw_ = 0;
    br->bsz_ = 0;
    *too_long = true;

    return false;
}

int br_block(buf_reader* br, const char** out) {
    if (br->cap_ == 0 || br->buf_ == NULL) {
        return 0;
    }
    if (br->mem_) {
        if (br->bpos_ == br->bsz_) {
            return 0;
        }
    } else {
        if (br->fh_ == 0) {
            return 0;
        }
        if (br->bpos_ == br->bsz_) {
            uint24_t frsz = mos_fread(br->fh_, br->buf_, br->cap_);
            if (frsz == 0) {
                br->cap_ = 0;
    br->raw_ = 0;
    br->bsz_ = 0;

                return 0;
            }
            br->bpos_ = 0;
            br->raw_ = frsz;
            br->bsz_ = frsz;
        }
    }

    *out = &br->buf_[br->bpos_];
    const int n = (int) (br->bsz_ - br->bpos_);
    br->bpos_ = br->bsz_;

    return n;
}

int br_byte(buf_reader* br) {
    if (br->cap_ == 0) {
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
        uint24_t frsz = mos_fread(br->fh_, br->buf_, br->cap_);
        if (frsz == 0) {
            br->cap_ = 0;
    br->raw_ = 0;
    br->bsz_ = 0;

            return -1;
        }
        br->bpos_ = 0;
        br->raw_ = frsz;
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
