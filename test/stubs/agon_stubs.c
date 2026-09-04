/* Host-test implementations of the MOS API zap uses.
 *
 * Files are real stdio files: the lexer's job is to read bytes off disk and a
 * stub that faked them would not exercise buf_reader's refill path. The VDU
 * side goes to stdout, which is where the tests want the trace anyway. */
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#define MAX_FH 8

static FILE* files[MAX_FH + 1];
static FIL   fils[MAX_FH + 1];

uint8_t mos_fopen(const char* fname, uint8_t mode) {
    const char* m = (mode & FA_CREATE_ALWAYS) ? "wb+" : "rb+";
    FILE* f = fopen(fname, m);
    if (f == NULL && !(mode & FA_CREATE_ALWAYS)) {
        f = fopen(fname, "rb");
    }
    if (f == NULL) {
        return 0;
    }

    for (uint8_t i = 1; i <= MAX_FH; i++) {
        if (files[i] == NULL) {
            files[i] = f;
            fseek(f, 0, SEEK_END);
            fils[i].obj.objsize = (uint32_t) ftell(f);
            fseek(f, 0, SEEK_SET);

            return i;
        }
    }
    fclose(f);

    return 0;
}

uint8_t mos_fclose(uint8_t fh) {
    if (fh == 0 || fh > MAX_FH || files[fh] == NULL) {
        return 0;
    }
    fclose(files[fh]);
    files[fh] = NULL;

    return 0;
}

uint24_t mos_fread(uint8_t fh, char* buf, uint24_t n) {
    if (fh == 0 || fh > MAX_FH || files[fh] == NULL) {
        return 0;
    }

    return (uint24_t) fread(buf, 1, n, files[fh]);
}

uint24_t mos_fwrite(uint8_t fh, char* buf, uint24_t n) {
    if (fh == 0 || fh > MAX_FH || files[fh] == NULL) {
        return 0;
    }

    return (uint24_t) fwrite(buf, 1, n, files[fh]);
}

uint8_t mos_flseek(uint8_t fh, uint32_t offset) {
    if (fh == 0 || fh > MAX_FH || files[fh] == NULL) {
        return 1;
    }
    fseek(files[fh], (long) offset, SEEK_SET);

    return 0;
}

FIL* mos_getfil(uint8_t fh) {
    if (fh == 0 || fh > MAX_FH || files[fh] == NULL) {
        return NULL;
    }

    return &fils[fh];
}

void mos_puts(const char* buf, uint24_t size, char delim) {
    (void) delim;
    if (size == 0) {
        fputs(buf, stdout);

        return;
    }
    fwrite(buf, 1, size, stdout);
}

int putch(int ch) {
    fputc(ch, stdout);

    return ch;
}
