#include "instruction_parser.h"

#include <mos_api.h>

#include "hash_table.h"
#include "parser.h"

#ifndef CONSUME
#define CONSUME(p, tk, msg) do { \
    if (next(p).tk_ != tk) { \
        return pr_msg(p, msg); \
    } \
} while (false)
#endif

extern int tk2i(token tk);
extern const char* pr_msg(parser* p, const char* msg);
extern token next(parser* p);
extern bool pr_wbyte(parser* p, uint8_t b);
extern void pr_stack_label(parser* p, char* label, int sz);
extern void pr_stack_relative_label(parser* p, char* label, int sz);

union _value {
    int i;
    uint8_t b[4];
} value;

typedef enum _isa_suffix {
    SUF_NONE = 0x00,
    SUF_ERR  = 0x01,
    SUF_SIS  = 0x40,
    SUF_SIL  = 0x52,
    SUF_LIS  = 0x49,
    SUF_LIL  = 0x5B
} isa_suffix;

// Assume we are parsing the suffix because we already found a dot.
static isa_suffix parse_suffix(parser* p) {
    const token tk = next(p);
    if (p->tk_.tk_ != NAME) {
        return SUF_ERR;
    }

    if (tk.sz_ != 1 && tk.sz_ != 3) {
        return SUF_ERR;
    }
    if (tk.txt_[1] != 'i' && tk.txt_[1] != 'I') {
        return SUF_ERR;
    }

    isa_suffix suf;
    switch (tk.txt_[0]) {
        case 's':
        case 'S':
            suf = SUF_SIL;
            break;
        case 'l':
        case 'L':
            suf = SUF_LIS;
            break;
        default:
            return SUF_ERR;
    }

    if (tk.sz_ == 3) {
        switch (tk.txt_[2]) {
            case 's':
            case 'S':
                if (suf == SUF_SIL) suf = SUF_SIS;
                break;
            case 'l':
            case 'L':
                if (suf == SUF_LIS) suf = SUF_LIL;
                break;
            default:
                return SUF_ERR;
        }
    }

    return suf;
}

static const char* parse_label_op(parser* p)  {
    const token tk = p->tk_;
    bool ok = false;
    int addr = ht_nget(&p->labels_, tk.txt_, tk.sz_, &ok);
    if (ok) {
        value.i = addr;
        for (uint8_t i = 0; i < 3; i++) {
            pr_wbyte(p, value.b[i]);
        }
    } else {
        pr_stack_label(p, tk.txt_, tk.sz_);
    }
    return NULL;
}

static const char* parse_relative_label_op(parser* p)  {
    const token tk = p->tk_;
    bool ok = false;
    int addr = ht_nget(&p->labels_, tk.txt_, tk.sz_, &ok);
    if (ok) {
        int d = p->pos_ - addr;
        if (d < -128 || d > 127) {
            return pr_msg(p, "too far");
        }
        pr_wbyte(p, (uint8_t) (p->pos_ - addr));
    } else {
        pr_stack_relative_label(p, tk.txt_, tk.sz_);
    }
    return NULL;
}


static void parse_number(parser* p) {
    value.i = tk2i(p->tk_);
    for (uint8_t i = 0; i < 3; i++) {
        pr_wbyte(p, value.b[i]);
    }
}

const char* parse_call(parser* p) {
    token tk = next(p);
    switch (tk.tk_) {
        case NAME:
            pr_wbyte(p, 0xCD);
            return parse_label_op(p);
        default:
            return pr_msg(p, "invalid address");
    }
    return NULL;
}

const char* parse_inc(parser* p) {
    CONSUME(p, REGISTER, "invalid operand");

    uint8_t isa;
    switch (p->tk_.tt_) {
        case REG_BC:
            isa = 0x03;
            break;
        case REG_DE:
            isa = 0x13;
            break;
        case REG_HL:
            isa = 0x23;
            break;
        default:
            return pr_msg(p, "invalid operand");
    }
    pr_wbyte(p, isa);
    return NULL;
}

const char* parse_jp(parser* p) {
    const token tk = next(p);
    switch (tk.tk_) {
        case NAME:
            pr_wbyte(p, 0xC3);
            return parse_label_op(p);
        case HEX_NUMBER:
        case NUMBER:
            pr_wbyte(p,  0xC3);
            parse_number(p);
            break;
        default:
            return pr_msg(p, "expeted an address or a label.");
    }
    return NULL;
}

const char* parse_jr(parser* p) {
    pr_wbyte(p, 0x18);
    const token tk = next(p);
    switch (tk.tk_) {
        case NAME:
            return parse_relative_label_op(p);
        case HEX_NUMBER:
        case NUMBER:
            value.i = tk2i(tk);
            if (value.i < -128 || value.i > 127) {
                return pr_msg(p, "invalid operand");
            }
            pr_wbyte(p, (uint8_t) value.i);
            break;
        default:
            return pr_msg(p, "invalid operand");
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
                case NAME:
                    pr_wbyte(p, isa);
                    return parse_label_op(p);
                case NUMBER:
                case HEX_NUMBER:
                    pr_wbyte(p, 0x21);
                    parse_number(p);
                    return NULL;
                default:
                    return pr_msg(p, "invalid operand");
            }

            break;
        default:
            return pr_msg(p, "invalid operand");
    }
    return NULL;
}

const char* parse_or(parser* p) {
    CONSUME(p, REGISTER, "expected register");
    if (p->tk_.tt_ != REG_A) {
        return pr_msg(p, "expected A reg");
    }
    token tk = next(p);
    if (tk.tk_ == NEW_LINE) {
        // We are ORing A with A.
        pr_wbyte(p, 0xB7);
        return NULL;
    }

    if (tk.tk_ != COMMA) {
        return pr_msg(p, "expected comma");
    }
    CONSUME(p, REGISTER, "expected register");
    uint8_t isa;
    switch (p->tk_.tt_) {
        case REG_A:
            isa = 0xB7;
            break;
        case REG_B:
            isa = 0xB0;
            break;
        case REG_C:
            isa = 0xB1;
            break;
        case REG_D:
            isa = 0xB2;
            break;
        case REG_E:
            isa = 0xB3;
            break;
        case REG_H:
            isa = 0xB4;
            break;
        case REG_L:
            isa = 0xB5;
            break;
        default:
            return pr_msg(p, "invalid operand");
    }

    pr_wbyte(p, isa);
    return NULL;
}

const char* parse_ret(parser* p) {
    next(p);
    if (p->tk_.tk_ == NEW_LINE) {
        pr_wbyte(p, 0xC9);
        return NULL;
    }

    if (p->tk_.tk_ != FLAG) {
        return pr_msg(p, "expected flag");
    }

    uint8_t isa = 0xC0;  // assum F_NZ
    switch (p->tk_.tt_) {
        case F_Z:
            isa = 0xC8;
            break;
        case F_NC:
            isa = 0xD0;
            break;
        case F_C:
            isa = 0xD8;
            break;
        case F_PO:
            isa = 0xE0;
            break;
        case F_PE:
            isa = 0xE8;
            break;
        case F_P:
            isa = 0xF0;
            break;
        case F_M:
            isa = 0xF8;
            break;
        default:
            break;
    }
    pr_wbyte(p, isa);
    return NULL;
}

const char* parse_rst(parser* p) {
    next(p);

    isa_suffix suf = SUF_NONE;
    if (p->tk_.tk_ == DOT) {
        suf = parse_suffix(p);
        if (suf == SUF_ERR) {
            return pr_msg(p, "invalid suffix");
        }
    }

    next(p);
    if (p->tk_.tk_ != NUMBER && p->tk_.tk_ != HEX_NUMBER) {
        return pr_msg(p, "expected number");
    }
    if (suf != SUF_NONE) {
        pr_wbyte(p, (uint8_t) suf);
    }

    int v = tk2i(p->tk_);
    uint8_t isa;
    switch (v) {
        case 0x00:
            isa = 0xC7;
            break;
        case 0x08:
            isa = 0xCF;
            break;
        case 0x10:
            isa = 0xD7;
            break;
        case 0x18:
            isa = 0xDF;
            break;
        case 0x20:
            isa = 0xE7;
            break;
        case 0x28:
            isa = 0xEF;
            break;
        case 0x30:
            isa = 0xF7;
            break;
        case 0x38:
            isa = 0xFF;
            break;
        default:
            return pr_msg(p, "invalid operand");
    }
    pr_wbyte(p, isa);

    return NULL;
}
