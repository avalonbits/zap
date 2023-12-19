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
    if (parser_init(&p, argv[1]) == NULL) {
        return -1;
    }
    const char* errmsg = parser_parse(&p, NULL, 0);
    if (errmsg != NULL && strlen(errmsg) > 0) {
        mos_puts(errmsg, 0, 0);
    }

    parser_destroy(&p);

    return 0;
}
