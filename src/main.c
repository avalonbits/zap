#include <string.h>
#include <mos_api.h>

#include "conv.h"
#include "lexer.h"
#include "parser.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        mos_puts("\r\nUsage: zap <filename>\r\n", 0, 0);
        return 0;
    }
    mos_puts("Assembling ", 0, 0);
    mos_puts(argv[1], 0, 0);
    mos_puts("\r\n", 0, 0);

    parser p;
    if (pr_init(&p, argv[1]) == NULL) {
        return -1;
    }

    const char* errmsg = pr_parse(&p);
    if (errmsg != NULL && strlen(errmsg) > 0) {
        mos_puts(errmsg, 0, 0);
    }

    pr_destroy(&p);

    return 0;
}
