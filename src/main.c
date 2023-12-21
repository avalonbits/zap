#include <stdio.h>
#include <string.h>
#include <mos_api.h>

#include "lexer.h"
#include "parser.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        mos_puts("\r\nUsage: zap <filename>\r\n", 0, 0);
        return 0;
    }

    parser p;
    if (pr_init(&p, argv[1]) == NULL) {
        return -1;
    }

    const char* errmsg = pr_parse(&p);
    if (errmsg != NULL && strlen(errmsg) > 0) {
        mos_puts(errmsg, 0, 0);
    }


    for (int i = 0; i < p.pos_; i++) {
        if (i % 16 == 0) {
            printf("\r\n");
        }
        printf("%02X ", p.buf_[i]);
    }
    printf("\r\n");
    pr_destroy(&p);

    return 0;
}
