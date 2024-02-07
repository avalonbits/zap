#include <string.h>
#include <mos_api.h>

#include "conv.h"
#include "lexer.h"
#include "parser.h"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        mos_puts("\r\nUsage: zap <filename> [outfile] \r\n", 0, 0);
        return 0;
    }
    mos_puts("Assembling ", 0, 0);
    mos_puts(argv[1], 0, 0);
    mos_puts("\r\n\r\n", 0, 0);

    parser p;
    if (pr_init(&p, argv[1]) == NULL) {
        return 1;
    }

    const char* errmsg = pr_parse(&p);
    if (errmsg != NULL && strlen(errmsg) > 0) {
        mos_puts((char*)errmsg, 0, 0);
        return 1;
    }

    char fname[255];
    if (argc == 3) {
        uint8_t i = 0;
        for (; argv[2][i] != 0; i++) {
            fname[i] = argv[2][i];
        }
        fname[i] = 0;
    } else {
        uint8_t i = 0;
        for (; argv[1][i] != 0 && argv[1][i] != '.'; i++) {
            fname[i] = argv[1][i];
        }
        if (fname[i] != '.') {
            fname[i] = '.';
        }
        fname[++i] = 'b';
        fname[++i] = 'i';
        fname[++i] = 'n';
        fname[++i] = 0;
    }

    uint8_t fh = mos_fopen(fname, FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        mos_puts("unable to write to ", 0, 0);
        mos_puts(fname, 0, 0);
        return -1;
    }

    int sz = 0;
    char* buf = (char*) pr_buf(&p, &sz);
    if (sz > 0) {
        mos_puts("Writting ", 0, 0);
        mos_puts(fname, 0, 0);
        mos_fwrite(fh, buf, sz);
    }

    pr_destroy(&p);
    mos_fclose(fh);

    return 0;
}
