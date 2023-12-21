#include "instruction_parser.h"

#include "hash_table.h"
#include "parser.h"

extern int tk2i(token tk);
extern const char* pr_msg(parser* p, const char* msg);
extern token next(parser* p);
extern bool pr_wbyte(parser* p, uint8_t b);
extern void pr_stack_label(parser* p, char* label, int sz);

union _value {
    int i;
    uint8_t b[4];
} value;

const char* parse_jp(parser* p) {
    token tk = next(p);
    switch (tk.tk_) {
        case NAME: {
            pr_wbyte(p, 0xC3);
            bool ok = false;
            int addr = ht_nget(&p->labels_, tk.txt_, tk.sz_, &ok);
            if (ok) {
                value.i = addr;
                for (uint8_t i = 0; i < 3; i++) {
                    pr_wbyte(p, value.b[i]);
                }
            } else {
                pr_stack_label(p, tk.txt_, tk.sz_);
                return NULL;
            }
        }
        case HEX_NUMBER:
        case NUMBER:
            value.i = tk2i(tk);
            if (value.i >= -128 && value.i <= 127) {
                pr_wbyte(p, 0x18);
                pr_wbyte(p, value.b[0]);
            } else {
                pr_wbyte(p,  0xC3);
                for (uint8_t i = 0; i < 3; i++) {
                    pr_wbyte(p, value.b[i]);
                }
            }
            break;
        default:
            return pr_msg(p, "expeted an address or a label.");
    }
    return NULL;
}
