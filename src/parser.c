#include "parser.h"

#include <mos_api.h>
#include <stdlib.h>
#include <string.h>

#include "conv.h"
#include "instruction_parser.h"
#include "lexer.h"

#define PUTS(msg) mos_puts(msg, 0, 0)
#define TK_EQ(msg1, tk) (strncmp(msg1, upper(tk.txt_, tk.sz_), tk.sz_) == 0)

static char* upper(char* str, int sz) {
    for (int i = 0; i < sz; i++) {
        const char ch = str[i];
        if (ch >= 0x61 && ch <= 0x7A) {
            str[i] = ch - 0x20;
        }
    }
    return str;
}

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
    strcpy(errmsg, "\r\nLine ");
    i2s(p->lex_.lcount_, &errmsg[7], sizeof(errmsg) - 5);
    strcat(errmsg, ": ");
    strcat(errmsg, msg);
    return errmsg;
}

static const char* parse_adl(parser* p) {
    token tk = next(p);
    if (tk.tk_ != DIRECTIVE || !TK_EQ("ADL", tk)) {
        return pr_msg(p, "expected ADL");
    }

    if (next(p).tk_ != EQUALS) {
        return pr_msg(p, "expected =");
    }

    tk = next(p);
    if (tk.tk_ != NUMBER || (!TK_EQ("1", tk) && !TK_EQ("0", tk))) {
        return pr_msg(p, "ADL is 0 or 1");
    }
    p->adl_ = tk.txt_[0] == '1';

    return NULL;
}

static int natoi(uint8_t* str, uint8_t sz) {
    int v = 0;
    int mul = 1;
    for (uint8_t i = 1; i <= sz; i++) {
        v += (str[sz-i] - '0') * mul;
        mul *= 10;
    }
    return v;
}

static int natoh(uint8_t* str, uint8_t sz) {
    int v = 0;
    int mul = 1;
    for (uint8_t i = 1; i <= sz; i++) {
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

static const char* parse_db(parser* p) {
    int mul = 1;
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
                p->skip_ws_ = false;
                while (true) {
                    tk = next(p);
                    if (tk.tk_ == D_QUOTE) {
                        break;
                    }
                    if (tk.tk_ == NEW_LINE) {
                        return pr_msg(p, "missing ending quote.");
                    }
                    for (int i = 0; i < tk.sz_; i++) {
                        pr_wbyte(p, tk.txt_[i]);
                    }
                }
                p->skip_ws_ = true;
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
    if (TK_EQ("ASSUME", p->tk_)) {
        return parse_adl(p);
    } else if (TK_EQ("ORG", p->tk_)) {
        return parse_org(p);
    } else if (TK_EQ("ALIGN", p->tk_)) {
        return parse_align(p);
    } else if (TK_EQ("DB", p->tk_)) {
        return parse_db(p);
    }

    return NULL;
}

static const char* parse_start_dot(parser* p) {
    p->tk_ = next(p);
    if (p->tk_.tk_ != DIRECTIVE) {
        return pr_msg(p, "expected a directive after the dot");
    }
    return parse_directive(p);
}

static const char* parse_instruction(parser* p) {
    if (TK_EQ("JP", p->tk_)) {
        return parse_jp(p);
    }
    return pr_msg(p, "found nothing");
}

union _v {
    int i;
    uint8_t b[4];
} v;


static const char* post_process(parser* p) {
    bool ok;
    for (const label_node* ln = ls_pop(&p->ls_); ln != NULL; ln = ls_pop(&p->ls_)) {
        ok = false;
        v.i = ht_get(&p->labels_, ln->label_, &ok);
        if (!ok) {
            p->lex_.lcount_ = ln->line_;
            return pr_msg(p, "label does not exist.");
        }

        p->pos_ = ln->bpos_;
        for (uint8_t i = 0; i < 3; i++) {
            pr_wbyte(p, v.b[i]);
        }
    }
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
            case NEW_LINE:
                continue;
            default:
                break;
        }
        if (err != NULL) {
            return err;
        }

        // If we processed correctly, we are at the end of the line.
        if (p->tk_.tk_ == NEW_LINE) {
            continue;
        }

        p->tk_ = next(p);
        if (p->tk_.tk_ != NEW_LINE) {
            return pr_msg(p, "expected a new line.");
        }
    }

    return post_process(p);
}

