#include "parser.h"

#include <agon/mos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conv.h"
#include "expr.h"
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

/* Builds the table key for a name. A global name is itself; a local one -- any
 * name starting with '@' -- is prefixed with the scope it was written in, so
 * @loop in one routine and @loop in the next do not collide. The prefix is two
 * bytes rather than the enclosing label's text, which keeps the key inside
 * MAX_NAME however long that label is. */
static int scoped_key(parser* p, const char* name, int sz, char* out) {
    int n = 0;
    if (sz > 0 && name[0] == '@') {
        out[n++] = (char) (1 + (p->scope_ & 0x7F));
        out[n++] = '@';
    }
    if (sz > MAX_NAME - n) {
        return -1;
    }
    for (int i = 0; i < sz; i++) {
        out[n++] = name[i];
    }

    return n;
}

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
        ht_destroy(&p->labels_);
        lex_destroy(&p->lex_);

        return NULL;
    }

    if (ls_init(&p->ls_, 1024) == NULL) {
        /* This used to fall through and hand back a parser whose label stack
         * had never been allocated. */
        free(p->buf_);
        ht_destroy(&p->labels_);
        lex_destroy(&p->lex_);

        return NULL;
    }

    /* The Agon load address for an ordinary program, and the reference's
     * default (START_ADDRESS in its config.h). It was 0x400000 here, one zero
     * too many, so every address in a source without an .ORG was wrong. */
    p->org_ = 0x40000;
    p->addr_ = p->org_;
    p->scope_ = 0;
    p->last_label_sz_ = 0;
    p->anon_count_ = 0;
    p->fname_ = fname;
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
    p->buf_ = NULL;
    ls_destroy(&p->ls_);
    ht_destroy(&p->labels_);
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
            default:
                /* The comment check used to sit below a DIRECTIVE case, so a
                 * directive name inside a comment was handed to the parser as
                 * a real directive. "; starting at byte 64." was enough to
                 * break a file, because "byte" is a directive. */
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

/* Which anonymous label a name refers to, or -2 if it is not one of them.
 * @b and @p look back, @f and @n look forward. */
static int anon_ref(const char* name, int sz) {
    if (sz != 2 || name[0] != '@') {
        return -2;
    }
    switch (name[1]) {
        case 'b': case 'B':
        case 'p': case 'P':
            return -1;   /* the one before here */
        case 'f': case 'F':
        case 'n': case 'N':
            return 1;    /* the one after here */
        default:
            return -2;
    }
}

/* Resolves a name to its value. Sets *known false when it cannot be resolved
 * yet, which is a forward reference and the caller's cue to leave a hole. */
const char* pr_resolve(parser* p, const char* name, int sz, value* out,
                       bool* known, int* anon) {
    *known = false;
    *anon = -1;

    const int a = anon_ref(name, sz);
    if (a == -1) {
        if (p->anon_count_ == 0) {
            return pr_msg(p, "no anonymous label before here");
        }
        *out = (value) p->anon_[p->anon_count_ - 1];
        *known = true;

        return NULL;
    }
    if (a == 1) {
        /* The next @@ to be defined; it does not exist yet by definition. */
        *anon = p->anon_count_;

        return NULL;
    }

    char key[MAX_NAME + 1];
    const int ksz = scoped_key(p, name, sz, key);
    if (ksz < 0) {
        return pr_msg(p, "label too long");
    }

    bool ok = false;
    const int v = ht_nget(&p->labels_, key, (uint8_t) ksz, &ok);
    if (ok) {
        *out = (value) v;
        *known = true;
    }

    return NULL;
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
    value v = 0;
    const char* err = expr_eval(p, &v);
    if (err != NULL) {
        return err;
    }
    if (v != 0 && v != 1) {
        return pr_msg(p, "ADL is 0 or 1");
    }
    p->adl_ = v == 1;

    return NULL;
}

/* The lexer converts a literal as it reads it, so this is now only a guard
 * against being handed a token that is not a number at all. It used to
 * re-parse the token text, with a hand-rolled decimal routine that read the
 * leading '-' as a digit and returned -25 for "-5". */
value tk2i(token tk) {
    if (tk.tk_ != NUMBER) {
        return 0;
    }

    return tk.val_;
}

bool pr_wbyte(parser* p, uint8_t b) {
    if (p->pos_ == p->sz_) {
        return false;
    }
    p->buf_[p->pos_++] = b;
    p->addr_++;

    return true;
}

/* Leaves a hole of the right width and remembers how to fill it. */
static const char* stack_fixup(parser* p, char* label, int sz, fixup_kind kind,
                               int anon) {
    const int width = (kind == FIX_REL8) ? 1 : (kind == FIX_ABS16 ? 2 : 3);
    const int bpos = p->pos_;

    for (int i = 0; i < width; i++) {
        if (!pr_wbyte(p, 0)) {
            return pr_msg(p, "output too large");
        }
    }

    /* addr_ is now the address of the next instruction, which is exactly what
     * a relative displacement is measured from. */
    if (!ls_push(&p->ls_, label, sz, bpos, p->addr_, p->lex_.lcount_,
                 kind, p->scope_, anon)) {
        return pr_msg(p, "label too long");
    }

    return NULL;
}

const char* pr_stack_label(parser* p, char* label, int sz, int anon) {
    return stack_fixup(p, label, sz, p->adl_ ? FIX_ABS24 : FIX_ABS16, anon);
}

const char* pr_stack_relative_label(parser* p, char* label, int sz, int anon) {
    return stack_fixup(p, label, sz, FIX_REL8, anon);
}

static const char* parse_org(parser* p) {
    next(p);

    value v = 0;
    const char* err = expr_eval(p, &v);
    if (err != NULL) {
        return err;
    }
    p->org_ = v;
    p->addr_ = v;

    return NULL;
}

static const char* parse_align(parser* p) {
    next(p);

    value align = 0;
    const char* err = expr_eval(p, &align);
    if (err != NULL) {
        return err;
    }

    /* Still aligning to an absolute offset rather than to a boundary. That is
     * wrong, and it is fixed with the rest of the directives; changing it here
     * would put a byte-level change in a commit that is meant to be about
     * where values come from. */
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
    next(p);

    while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
        if (p->tk_.tk_ == D_QUOTE) {
            const char* err = parse_quoted(p);
            if (err != NULL) {
                return err;
            }
            next(p);
        } else {
            /* Every element is a full expression now, so db 'a'+1 and
             * db 128+127-255 work the same way the reference reads them. */
            value v = 0;
            const char* err = expr_eval(p, &v);
            if (err != NULL) {
                return err;
            }
            /* Out of range truncates rather than failing. The reference
             * warns ("Value truncated to 8 bit") and carries on emitting the
             * low byte, so refusing here would diverge on any source it
             * accepts. zap has nowhere to put a warning until diagnostics
             * exist; the byte is what has to match. */
            pr_wbyte(p, (uint8_t) (v & 0xFF));
        }

        // Either a comma, because there is more to process, or the end of the line.
        if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == NONE) {
            return NULL;
        }
        if (p->tk_.tk_ != COMMA) {
            return pr_msg(p, "expected a comma");
        }
        next(p);
    }

    return NULL;
}

/* "name: equ <expr>" redefines the label on the same line to the expression's
 * value instead of the address it was given when it was read. */
static const char* parse_equ(parser* p) {
    if (p->last_label_sz_ == 0) {
        return pr_msg(p, "equ needs a label");
    }

    next(p);
    value v = 0;
    const char* err = expr_eval(p, &v);
    if (err != NULL) {
        return err;
    }

    if (!ht_nset(&p->labels_, p->last_label_, (uint8_t) p->last_label_sz_, v)) {
        return pr_msg(p, "label too long");
    }

    return NULL;
}

static const char* parse_directive(parser* p) {
    switch (p->tk_.tt_) {
        case D_ASSUME:
            return parse_adl(p);
        case D_EQU:
            return parse_equ(p);
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
            return pr_msg(p, "invalid instruction");
    }
    return NULL;
}

static const char* parse_label(parser* p) {
    if (!p->tk_.label_) {
        return pr_msg(p, "expected a colon");
    }

    /* An anonymous label has no name to look up -- it is found by position. */
    if (p->tk_.sz_ == 2 && p->tk_.txt_[0] == '@' && p->tk_.txt_[1] == '@') {
        if (p->anon_count_ == (int) (sizeof(p->anon_) / sizeof(p->anon_[0]))) {
            return pr_msg(p, "too many anonymous labels");
        }
        p->anon_[p->anon_count_++] = p->addr_;

        next(p);

        return NULL;
    }

    char key[MAX_NAME + 1];
    const int ksz = scoped_key(p, p->tk_.txt_, p->tk_.sz_, key);
    if (ksz < 0) {
        return pr_msg(p, "label too long");
    }

    /* A global label opens a new local scope, so the names inside the routine
     * that follows are distinct from the ones before it. */
    if (p->tk_.txt_[0] != '@') {
        p->scope_++;
    }

    /* A name too long for the table used to be dropped silently, so the label
     * simply did not exist and every reference to it failed later with no
     * hint why. */
    if (!ht_nset(&p->labels_, key, ksz, p->addr_)) {
        return pr_msg(p, "label too long");
    }

    for (int i = 0; i < ksz; i++) {
        p->last_label_[i] = key[i];
    }
    p->last_label_sz_ = ksz;

    /* The colon is already known to be there -- the lexer only flags a name as
     * a label when one follows immediately -- so just consume it. */
    next(p);

    return NULL;
}

/* Fills in every reference that was still unresolved when its line was read.
 *
 * The old version compared p->pos_-1 against 0x18 -- a buffer offset against
 * the JR opcode -- to decide relative from absolute, and then appended through
 * pr_wbyte instead of writing at the hole, so a forward JR overwrote whatever
 * followed it. Both are recorded on the fixup now. */
static const char* post_process(parser* p) {
    for (const label_node* ln = ls_pop(&p->ls_); ln != NULL; ln = ls_pop(&p->ls_)) {
        char key[MAX_NAME + 1];
        int ksz = 0;
        for (const char* c = ln->label_; *c != 0; c++) {
            ksz++;
        }

        /* A local name resolves in the scope it was written in, not the one
         * the file happened to end in. */
        int n = 0;
        if (ln->label_[0] == '@') {
            key[n++] = (char) (1 + (ln->scope_ & 0x7F));
            key[n++] = '@';
        }
        for (int i = 0; i < ksz; i++) {
            key[n + i] = ln->label_[i];
        }
        ksz += n;

        int v;
        if (ln->anon_ >= 0) {
            if (ln->anon_ >= p->anon_count_) {
                p->lex_.lcount_ = ln->line_;

                return pr_msg(p, "no anonymous label after here");
            }
            v = p->anon_[ln->anon_];
        } else {
            bool ok = false;
            v = ht_nget(&p->labels_, key, (uint8_t) ksz, &ok);
            if (!ok) {
                p->lex_.lcount_ = ln->line_;

                return pr_msg(p, "label does not exist.");
            }
        }

        if (ln->kind_ == FIX_REL8) {
            const int d = v - ln->next_;
            if (d < -128 || d > 127) {
                p->lex_.lcount_ = ln->line_;

                return pr_msg(p, "relative jump too far");
            }
            p->buf_[ln->bpos_] = (uint8_t) (d & 0xFF);
            continue;
        }

        const int width = (ln->kind_ == FIX_ABS16) ? 2 : 3;
        for (int i = 0; i < width; i++) {
            p->buf_[ln->bpos_ + i] = (uint8_t) ((v >> (i * 8)) & 0xFF);
        }
    }

    return NULL;
}

/* A cheap sweep for constant definitions, run before the real pass.
 *
 * It looks only for "name: equ <expr>" and evaluates the ones whose value is
 * already computable. It never tracks the program counter and never sizes an
 * instruction, so it costs a lex of the source and nothing more -- this is not
 * a second assembly pass.
 *
 * What it buys is the ordering freedom the reference has: a constant can be
 * used before the line that defines it, so "db before, after1" works with
 * after1 defined further down. A definition it cannot fold yet -- one that
 * depends on a label's address -- is skipped and left to the main pass, where
 * a label reference goes through the fixup path anyway.
 *
 * Failures are deliberately silent. Anything genuinely wrong is reported by
 * the real pass, with the right line number and without this one having to
 * guess whether a name it has not reached yet is a mistake. */
static void pr_prescan(parser* p) {
    lexer saved_lex = p->lex_;
    const uint8_t saved_scope = p->scope_;
    const int saved_pos = p->pos_;

    if (lex_init(&p->lex_, p->fname_) == NULL) {
        p->lex_ = saved_lex;

        return;
    }
    p->scope_ = 0;
    p->pos_ = 0;

    token tk = next(p);
    while (tk.tk_ != NONE) {
        if (tk.tk_ != NAME) {
            while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
                next(p);
            }
            tk = next(p);
            continue;
        }

        /* Copy the name before advancing: the token text lives in the lexer's
         * shared line buffer and the next token overwrites it. */
        char name[MAX_NAME + 1];
        int nsz = tk.sz_;
        if (nsz > MAX_NAME) {
            nsz = MAX_NAME;
        }
        for (int i = 0; i < nsz; i++) {
            name[i] = tk.txt_[i];
        }

        const bool global = name[0] != '@';
        if (next(p).tk_ == COLON) {
            if (global) {
                p->scope_++;
            }

            if (next(p).tt_ == D_EQU) {
                next(p);
                value v = 0;
                if (expr_eval(p, &v) == NULL) {
                    char key[MAX_NAME + 1];
                    const int ksz = scoped_key(p, name, nsz, key);
                    if (ksz > 0) {
                        ht_nset(&p->labels_, key, (uint8_t) ksz, v);
                    }
                }
            }
        }

        while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
            next(p);
        }
        tk = next(p);
    }

    br_destroy(&p->lex_.rd_);
    p->lex_ = saved_lex;
    p->scope_ = saved_scope;
    p->pos_ = saved_pos;
}

const char* pr_parse(parser* p) {
    // top level parser. On every iteration we are at the beginning of a new line.
    pr_prescan(p);

    p->pos_ = 0;
    p->addr_ = p->org_;
    p->scope_ = 0;
    p->anon_count_ = 0;
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
        /* End of file ends the last line just as well as a newline does. A
         * source whose final line had no trailing newline used to be rejected
         * outright -- and files in the reference corpus are written that
         * way. */
        if (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
            return pr_msg(p, "expected a new line.");
        }
    }

    return post_process(p);
}

