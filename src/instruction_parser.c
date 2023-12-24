#include "instruction_parser.h"

#include "hash_table.h"
#include "parser.h"

#define CONSUME(p, tk, msg) do { \
    next(p); \
    if (p->tk_.tk_ != tk) { \
        return pr_msg(p, msg); \
    } \
} while (false)

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
    const token tk = next(p);
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
            pr_wbyte(p,  0xC3);
            for (uint8_t i = 0; i < 3; i++) {
                pr_wbyte(p, value.b[i]);
            }
            break;
        default:
            return pr_msg(p, "expeted an address or a label.");
    }
    return NULL;
}

static const char* parse_ld_a(parser* p) {
    CONSUME(p, COMMA, "expected comma");
    CONSUME(p, L_PAREN, "expected L-paren");

    CONSUME(p, REGISTER, "expected register");
    if (p->tk_.tt_ != REG_HL) {
        return pr_msg(p, "expected HL");
    }

    CONSUME(p, R_PAREN, "expected R-paren");
    pr_wbyte(p, 0x7E);

    return NULL;
}

const char* parse_ld(parser* p) {
    next(p);
    uint8_t isa = 0;
    switch (p->tk_.tk_) {
        case REGISTER:
            switch (p->tk_.tt_) {
                case REG_BC:
                    isa = 0x01;
                    break;
                case REG_DE:
                    isa = 0x11;
                    break;
                case REG_HL:
                    isa = 0x21;
                    break;
                case REG_A:
                    return parse_ld_a(p);
                default:
                    return pr_msg(p, "expected regisater");
            }

            next(p);
            if (p->tk_.tk_ != COMMA) {
                return pr_msg(p, "expected comma");
            }

            next(p);
            switch (p->tk_.tk_) {
                case NAME: {
                    pr_wbyte(p, isa);
                    bool ok = false;
                    int addr = ht_nget(&p->labels_, p->tk_.txt_, p->tk_.sz_, &ok);
                    if (ok) {
                        value.i = addr;
                        for (uint8_t i = 0; i < 3; i++) {
                            pr_wbyte(p, value.b[i]);
                        }
                        return NULL;
                    } else {
                        pr_stack_label(p, p->tk_.txt_, p->tk_.sz_);
                        return NULL;
                    }
                }
                default:
                    break;
            }

            break;
        default:
            return pr_msg(p, "invalid operand.");
    }
    return NULL;
}

const char* parse_ret(parser* p) {
    pr_wbyte(p, 0xC9);
    return NULL;
}

const char* parse_ascii(struct _parser* p) {
    CONSUME(p, D_QUOTE, "expected quote");
    p->skip_ws_ = false;

    next(p);
    while (p->tk_.tk_ != D_QUOTE && p->tk_.tk_ != NEW_LINE) {
        for (int i = 0; i < p->tk_.sz_; i++) {
            pr_wbyte(p, p->tk_.txt_[i]);
        }
        next(p);
    }

    if (p->tk_.tk_ != D_QUOTE) {
        return pr_msg(p, "expected quote");
    }
    return NULL;
}

const char* parse_asciz(struct _parser* p) {
    const char* msg = parse_ascii(p);
    if (msg != NULL) {
        return msg;
    }

    pr_wbyte(p, 0);
    return NULL;
}

