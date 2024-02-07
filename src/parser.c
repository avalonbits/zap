#include "parser.h"

#include <mos_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conv.h"
#include "instruction_parser.h"
#include "lexer.h"

#define PUTS(msg) mos_puts(msg, 0, 0)
#ifndef CONSUME
#define CONSUME(p, tk, msg) do { \
    if (next(p).tk_ != tk) { \
        return pr_msg(p, msg); \
    } \
} while (false)
#endif


static char errmsg[256] = "";

parser* pr_init(parser* p, const char* fname) {
    if (lex_init(&p->lex_, fname) == NULL) {
        return NULL;
    }
    if (ht_init(&p->labels_, 255) == 0) {
        lex_destroy(&p->lex_);
        return NULL;
    }

    p->sz_ = 128 << 10;
    p->buf_ = (uint8_t*) malloc(p->sz_ * sizeof(uint8_t));
    if (p->buf_ == NULL) {
        lex_destroy(&p->lex_);
        return NULL;
    }

    if (ls_init(&p->ls_, 1024) == NULL) {
        free(p->buf_);
        lex_destroy(&p->lex_);
    }

    p->org_ = 0x400000;
    p->adl_ = true;
    p->skip_ws_ = true;
    p->comment_ = false;
    return p;
}

uint8_t* pr_buf(parser* p, int* sz) {
    *sz = p->pos_;
    return p->buf_;
}


void pr_destroy(parser* p) {
    free(p->buf_);
    lex_destroy(&p->lex_);
}

token next(parser* p) {
    while (true) {
        p->tk_ = lex_next(&p->lex_);
        token tk = p->tk_;
        switch (tk.tk_) {
            case NONE:
                return tk;
            case WHITE_SPACE:
                if (p->skip_ws_) {
                    continue;
                }
                return tk;
            case NEW_LINE:
                p->comment_ = false;
                return tk;
            case SEMI_COLON:
                p->comment_ = true;
                continue;
            case DIRECTIVE:
                return tk;
            default:
                if (p->comment_) {
                    continue;
                }
                return tk;
        }
    }
}

const char* pr_msg(parser* p, const char* msg) {
    strcpy(errmsg, "Line ");
    i2s(p->lex_.lcount_, &errmsg[5], sizeof(errmsg) - 5);
    strcat(errmsg, ": ");
    strcat(errmsg, msg);
    strcat(errmsg, "\r\n");
    return errmsg;
}

static const char* parse_adl(parser* p) {
    next(p);
    if (p->tk_.tt_ != D_ADL) {
        return pr_msg(p, "expected ADL");
    }

    if (next(p).tk_ != EQUALS) {
        return pr_msg(p, "expected =");
    }

    next(p);
    if (p->tk_.tk_ != NUMBER || (p->tk_.txt_[0] != '1' && p->tk_.txt_[0] != '0')) {
        return pr_msg(p, "ADL is 0 or 1");
    }
    p->adl_ = p->tk_.txt_[0] == '1';

    return NULL;
}

static int natoi(char* str, uint8_t sz) {
    int v = 0;
    int mul = 1;
    for (char i = 1; i <= sz; i++) {
        v += (str[sz-i] - '0') * mul;
        mul *= 10;
    }
    return v;
}

static int natoh(char* str, uint8_t sz) {
    int v = 0;
    int mul = 1;
    for (char i = 1; i <= sz; i++) {
        const char ch = str[sz-i];
        if (ch >= '0' && ch <= '9') {
            v += (ch - '0') * mul;
        } else if (ch >= 'A' && ch <= 'F') {
            v += (ch - 'A' + 10) * mul;
        } else if (ch >= 'a' && ch <= 'f') {
            v += (ch - 'a' + 10) * mul;
        } else {
            break;
        }
        mul *= 16;
    }
    return v;
}

int tk2i(token tk) {
    if (tk.tk_ == NUMBER) {
        return natoi(tk.txt_, tk.sz_);
    } else if (tk.tk_ == HEX_NUMBER) {
        return natoh(tk.txt_, tk.sz_);
    }

    return -1;
}

bool pr_wbyte(parser* p, uint8_t b) {
    if (p->pos_ == p->sz_) {
        return false;
    }
    p->buf_[p->pos_++] = b;
    return true;
}

void pr_stack_label(parser* p, char* label, int sz) {
    ls_push(&p->ls_, label, sz, p->pos_, p->lex_.lcount_);
    p->pos_ += 3;
}

void pr_stack_relative_label(parser* p, char* label, int sz) {
    ls_push(&p->ls_, label, sz, p->pos_, p->lex_.lcount_);
    p->pos_ += 1;
}

static const char* parse_org(parser* p) {
    token tk = next(p);
    if (tk.tk_ != NUMBER && tk.tk_ != HEX_NUMBER) {
        return pr_msg(p, "expected number.");
    }
    p->org_ = tk2i(tk);
    return NULL;
}

static const char* parse_align(parser* p) {
    token tk = next(p);
    if (tk.tk_ != NUMBER && tk.tk_ != HEX_NUMBER) {
        return pr_msg(p, "expected number.");
    }

    int align = tk2i(tk);
    while (p->pos_ < align) {
        pr_wbyte(p, 0);
    }
    return NULL;
}

static const char* parse_quoted(parser* p) {
    p->skip_ws_ = false;
    next(p);
    while (p->tk_.tk_ != D_QUOTE && p->tk_.tk_ != NEW_LINE) {
        if (p->tk_.tk_ != B_SLASH) {
            for (int i = 0; i < p->tk_.sz_; i++) {
                pr_wbyte(p, p->tk_.txt_[i]);
            }
            next(p);
            continue;
        }

        next(p);
        if (p->tk_.tk_ != NAME) {
            return pr_msg(p, "missing escape");
        }

        char ch = p->tk_.txt_[0];
        switch (ch) {
            case 't':
                ch = 0x09;
                break;
            case 'n':
                ch = 0x0A;
                break;
            case 'r':
                ch = 0x0D;
                break;
            case '"':
                ch = 0x22;
                break;
            case '\'':
                ch = 0x27;
                break;
            case '\\':
                ch = 0x5C;
                break;
            default:
                return pr_msg(p, "wrong escape");
        }
        pr_wbyte(p, ch);
        next(p);
    }
    p->skip_ws_ = true;

    if (p->tk_.tk_ == NEW_LINE) {
        return pr_msg(p, "expected quote");
    }
    return NULL;

}

static const char* parse_ascii(parser* p) {
    CONSUME(p, D_QUOTE, "expected quote");
    return parse_quoted(p);
}

static const char* parse_asciz(struct _parser* p) {
    const char* msg = parse_ascii(p);
    if (msg != NULL) {
        return msg;
    }

    pr_wbyte(p, 0);
    return NULL;
}


static const char* parse_db(parser* p) {
    const char* err = NULL;
    for (token tk = next(p); tk.tk_ != NONE; tk = next(p)) {
        switch (tk.tk_) {
            case NUMBER:
            case HEX_NUMBER: {
                int v = tk2i(tk);
                if (v < -128 || v > 255)  {
                    return pr_msg(p, "expected a byte value.");
                }
                pr_wbyte(p, ((uint8_t) v) & 0xFF);
                break;
            }
            case D_QUOTE:
                err = parse_quoted(p);
                if (err != NULL) {
                    return err;
                }
                break;
            default:
                return pr_msg(p, "expected string or numbers");
        }

        // We either now have a comma because there is more info to process or a new line.
        tk = next(p);
        if (tk.tk_ == NEW_LINE) {
           return NULL;
        }
        if (tk.tk_ != COMMA) {
            return pr_msg(p, "expected a comma");
        }
    }
    return NULL;
}

static const char* parse_directive(parser* p) {
    switch (p->tk_.tt_) {
        case D_ASSUME:
            return parse_adl(p);
        case D_ORG:
            return parse_org(p);
        case D_ALIGN:
            return parse_align(p);
        case D_DB:
        case D_DEFB:
        case D_BYTE:
            return parse_db(p);
        case D_ASCII:
            return parse_ascii(p);
        case D_ASCIZ:
            return parse_asciz(p);
        default:
            while (p->tk_.tk_ != NEW_LINE) {
                next(p);
            }
    }

    return NULL;
}

static const char* parse_start_dot(parser* p) {
    next(p);
    if (p->tk_.tk_ != DIRECTIVE) {
        return pr_msg(p, "expected a directive after the dot");
    }
    return parse_directive(p);
}

static const char* parse_instruction(parser* p) {
    switch (p->tk_.tt_) {
        case ISA_CALL:
            return parse_call(p);
        case ISA_INC:
            return parse_inc(p);
        case ISA_JP:
            return parse_jp(p);
        case ISA_JR:
            return parse_jr(p);
        case ISA_LD:
            return parse_ld(p);
        case ISA_OR:
            return parse_or(p);
        case ISA_RET:
            return parse_ret(p);
        case ISA_RST:
            return parse_rst(p);
        default:
            while (p->tk_.tk_ != NEW_LINE) {
                next(p);
            }
    }
    return NULL;
}

static const char* parse_label(parser* p) {
    ht_nset(&p->labels_, p->tk_.txt_, p->tk_.sz_, p->pos_+p->org_);
    token tk = next(p);
    if (tk.tk_ != COLON) {
        return pr_msg(p, "expected a colon");
    }
    return NULL;
}

union _v {
    int i;
    uint8_t b[4];
} v;


static const char* post_process(parser* p) {
    const int pos = p->pos_;
    bool ok;
    for (const label_node* ln = ls_pop(&p->ls_); ln != NULL; ln = ls_pop(&p->ls_)) {
        ok = false;
        v.i = ht_get(&p->labels_, ln->label_, &ok);
        if (!ok) {
            p->lex_.lcount_ = ln->line_;
            return pr_msg(p, "label does not exist.");
        }
        if (p->pos_-1 == 0x18) {
            pr_wbyte(p, (uint8_t) v.i);
        } else {
            p->pos_ = ln->bpos_;
            for (uint8_t i = 0; i < 3; i++) {
                pr_wbyte(p, v.b[i]);
            }
        }
    }
    p->pos_ = pos;
    return NULL;
}

const char* pr_parse(parser* p) {
    // top level parser. On every iteration we are at the beginning of a new line.
    p->pos_ = 0;
    const char* err = NULL;

    for (p->tk_ = next(p); p->tk_.tk_ != NONE; p->tk_ = next(p)) {
        switch (p->tk_.tk_) {
            case DOT:
                err = parse_start_dot(p);
                break;
            case DIRECTIVE:
                err = parse_directive(p);
                break;
            case INSTRUCTION:
                err = parse_instruction(p);
                break;
            case NAME:
                err = parse_label(p);
                break;
            case NEW_LINE:
                continue;
            default:
                break;
        }
        if (err != NULL) {
            return err;
        }

        // If we processed correctly, we are either at the end of the line or at a label colon
        if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == COLON) {
            continue;
        }

        p->tk_ = next(p);
        if (p->tk_.tk_ != NEW_LINE) {
            return pr_msg(p, "expected a new line.");
        }
    }

    return post_process(p);
}

