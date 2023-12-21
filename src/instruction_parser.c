#include "instruction_parser.h"

#include "parser.h"

extern int tk2i(token tk);
extern const char* parser_msg(parser* p, const char* msg);
extern token next(parser* p);

union _value {
    int i;
    uint8_t b[4];
} value;

const char* parse_jp(parser* p) {
    char isa = 0xC3;
    token tk = next(p);
    switch (tk.tk_) {
        case HEX_NUMBER:
        case NUMBER:
            p->buf_[p->pos_++] = isa;
            value.i = tk2i(tk) + p->org_;
            for (uint8_t i = 0; i < 3; i++) {
                p->buf_[p->pos_++] = value.b[i];
            }
            break;
        default:
            return parser_msg(p, "expeted an address or a label.");
    }
    return NULL;
}
