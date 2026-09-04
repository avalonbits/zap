#include "parser.h"

#include <agon/mos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conv.h"
#include "zap.h"
#include "expr.h"
#include "macro.h"
#include "value.h"
#include "encode.h"
#include "isa.h"
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

int pr_addr(const parser* p) {
    if (p->reloc_) {
        return p->reloc_base_ + (p->addr_ - p->reloc_out_);
    }

    return p->addr_;
}

/* Builds the table key for a name. A global name is itself; a local one -- any
 * name starting with '@' -- is prefixed with the scope it was written in, so
 * @loop in one routine and @loop in the next do not collide. The prefix is two
 * bytes rather than the enclosing label's text, which keeps the key inside
 * MAX_NAME however long that label is. */
static int scope_prefix(uint16_t scope, char* out) {
    /* Two bytes, each biased so neither can be zero -- the table stores keys
     * as C strings. That gives 14 bits of scope. One byte masked to 0x7F was
     * not enough: a file with 128 global labels wrapped, and two routines'
     * @loop became the same symbol. */
    out[0] = (char) (1 + (scope & 0x7F));
    out[1] = (char) (1 + ((scope >> 7) & 0x7F));
    out[2] = '@';

    return 3;
}

#define MAX_SCOPE 0x3FFF

static int scoped_key(parser* p, const char* name, int sz, char* out) {
    int n = 0;
    if (sz > 0 && name[0] == '@') {
        n = scope_prefix(p->scope_, out);
    }
    if (sz > MAX_NAME - n) {
        return -1;
    }
    for (int i = 0; i < sz; i++) {
        out[n++] = name[i];
    }

    return n;
}

static parser* pr_setup(parser* p, const char* fname) {
    /* Labels are case-sensitive; only the reserved words are not. */
    if (ht_init(&p->labels_, 255, false) == 0) {
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
    p->start_ = p->org_;
    p->high_ = 0;
    p->fill_ = 0xFF;
    p->reloc_ = false;
    p->stmt_addr_ = p->addr_;
    p->stmt_line_ = 1;
    p->scope_ = 0;
    p->last_label_sz_ = 0;
    p->anon_count_ = 0;
    p->fname_ = fname;
    p->mem_ = NULL;
    p->mem_len_ = 0;
    p->inc_depth_ = 0;
    p->has_diag_ = false;
    mt_init(&p->macros_);
    p->expand_id_ = 0;
    p->macro_depth_ = 0;
    p->cond_depth_ = 0;
    p->skip_depth_ = 0;
    p->undefined_ = false;
    p->pc_used_ = false;
    p->adl_ = true;
    p->cpu_ = CPU_EZ80;
    p->skip_ws_ = true;
    p->comment_ = false;
    return p;
}

parser* pr_init(parser* p, const char* fname) {
    if (lex_init(&p->lex_, fname) == NULL) {
        return NULL;
    }

    return pr_setup(p, fname);
}

parser* pr_init_mem(parser* p, const char* text, int len, const char* name) {
    if (br_open_mem(&p->lex_.rd_, text, len) == NULL) {
        return NULL;
    }
    p->lex_.lcount_ = 1;

    int n = 0;
    if (name != NULL) {
        while (name[n] != 0 && n < (int) sizeof(p->lex_.fname_) - 1) {
            p->lex_.fname_[n] = name[n];
            n++;
        }
    }
    p->lex_.fname_[n] = 0;

    /* The reserved-word tables are built by lex_init, which a memory source
     * never calls. */
    lex_prime();

    if (pr_setup(p, NULL) == NULL) {
        return NULL;
    }

    /* The prescan re-reads from the same text, so assembling from memory
     * behaves exactly as assembling the same source from a file does. */
    p->mem_ = text;
    p->mem_len_ = len;

    return p;
}

uint8_t* pr_buf(parser* p, int* sz) {
    /* Only as far as something was actually written. Trailing padding from a
     * ds or align at the end of the file is not part of the output. */
    *sz = p->high_;

    return p->buf_;
}


void pr_destroy(parser* p) {
    /* Unwind anything an include or a macro expansion left suspended. A parse
     * that fails part way through one never pops back out, so without this
     * every source still on the stack leaks its read buffer. */
    while (p->inc_depth_ > 0) {
        br_destroy(&p->lex_.rd_);
        p->lex_ = p->inc_[--p->inc_depth_];
    }

    mt_destroy(&p->macros_);
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
                /* End of an included file: go back to the one that included
                 * it and keep reading, so the include reads as if its text
                 * had been written in place. */
                if (p->inc_depth_ > 0) {
                    br_destroy(&p->lex_.rd_);
                    --p->inc_depth_;
                    p->lex_ = p->inc_[p->inc_depth_];
                    p->scope_ = p->inc_scope_[p->inc_depth_];
                    if (p->inc_macro_[p->inc_depth_]) {
                        p->macro_depth_--;
                    }
                    p->comment_ = false;

                    /* The end of an included file ends the line as well. Its
                     * last line often has no trailing newline, and without
                     * this it ran on into the line after the .include. */
                    p->tk_.tk_ = NEW_LINE;
                    p->tk_.txt_ = p->lex_.line_;
                    p->tk_.sz_ = 0;
                    p->tk_.val_ = 0;
                    p->tk_.label_ = false;

                    return p->tk_;
                }

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
    /* The first error is the one worth keeping: assembly stops there, and
     * anything after it is a guess about a file the parser no longer
     * understands. */
    if (!p->has_diag_) {
        int n = 0;
        while (p->lex_.fname_[n] != 0 && n < ZAP_MAX_FILE - 1) {
            p->diag_.file[n] = p->lex_.fname_[n];
            n++;
        }
        p->diag_.file[n] = 0;
        p->diag_.line = p->stmt_line_ > 0 ? p->stmt_line_ : p->lex_.lcount_;

        n = 0;
        while (msg[n] != 0 && n < ZAP_MAX_MSG - 1) {
            p->diag_.msg[n] = msg[n];
            n++;
        }
        p->diag_.msg[n] = 0;
        p->has_diag_ = true;
    }

    errmsg[0] = 0;
    if (p->lex_.fname_[0] != 0) {
        strcat(errmsg, p->lex_.fname_);
        strcat(errmsg, " ");
    }
    strcat(errmsg, "line ");

    char num[16];
    i2s(p->lex_.lcount_, num, sizeof(num));
    strcat(errmsg, num);
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
    if (p->tk_.tk_ != DIRECTIVE || p->tk_.tt_ != D_ADL) {
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

/* Grows the output buffer to hold at least `need` bytes. The buffer cannot be
 * streamed out as it is produced -- a forward reference is patched in place
 * after the fact, so the bytes have to stay addressable -- which means it has
 * to grow instead. It started at a fixed 128KB, which is smaller than some
 * real sources need. */
static bool pr_reserve(parser* p, int need) {
    if (need <= p->sz_) {
        return true;
    }

    int want = p->sz_;
    while (want < need) {
        if (want > (1 << 22)) {
            return false;  /* 4MB is past anything an Agon can load */
        }
        want *= 2;
    }

    uint8_t* grown = (uint8_t*) realloc(p->buf_, (size_t) want * sizeof(uint8_t));
    if (grown == NULL) {
        return false;
    }
    p->buf_ = grown;
    p->sz_ = want;

    return true;
}

/* Writes one byte at the current address, filling any gap left behind by an
 * .org, ds or align that moved the address without writing. */
bool pr_wbyte(parser* p, uint8_t b) {
    const int at = p->addr_ - p->start_;
    if (at < 0 || !pr_reserve(p, at + 1)) {
        return false;
    }

    for (int i = p->high_; i < at; i++) {
        p->buf_[i] = p->fill_;
    }

    p->buf_[at] = b;
    p->addr_++;
    p->pos_ = at + 1;
    if (p->pos_ > p->high_) {
        p->high_ = p->pos_;
    }

    return true;
}

/* Moves the address without writing anything, which is what ds and align do.
 * The bytes appear only if something later lands past them. */
static void pr_skip(parser* p, int n) {
    p->addr_ += n;
    p->pos_ = p->addr_ - p->start_;
}

/* Leaves a hole of the right width and remembers how to fill it. */
const char* pr_stack_fixup(parser* p, const char* text, int sz,
                           fixup_kind kind, int anon) {
    const int width = (kind == FIX_REL8 || kind == FIX_ABS8) ? 1
                    : (kind == FIX_ABS16) ? 2
                    : (kind == FIX_ABS24) ? 3 : 4;
    const int bpos = p->pos_;

    for (int i = 0; i < width; i++) {
        if (!pr_wbyte(p, 0)) {
            return pr_msg(p, "output too large");
        }
    }

    /* addr_ is now the address of the next instruction, which is exactly what
     * a relative displacement is measured from. */
    if (!ls_push(&p->ls_, text, sz, bpos, pr_addr(p), p->stmt_addr_,
                 p->lex_.lcount_, kind, p->scope_, anon)) {
        return pr_msg(p, "too many forward references");
    }

    return NULL;
}

const char* pr_stack_label(parser* p, char* label, int sz, int anon) {
    return pr_stack_fixup(p, label, sz, p->adl_ ? FIX_ABS24 : FIX_ABS16, anon);
}

const char* pr_stack_relative_label(parser* p, char* label, int sz, int anon) {
    return pr_stack_fixup(p, label, sz, FIX_REL8, anon);
}

static const char* parse_org(parser* p) {
    next(p);

    value v = 0;
    const char* err = expr_eval(p, &v);
    if (err != NULL) {
        return err;
    }
    if (p->high_ == 0) {
        /* Nothing written yet, so this is where the output begins. */
        p->start_ = v;
    } else if (v < p->addr_) {
        return pr_msg(p, "new address lower than current address");
    }
    p->org_ = v;
    p->addr_ = v;
    p->pos_ = p->addr_ - p->start_;

    return NULL;
}

static const char* parse_align(parser* p) {
    next(p);

    value align = 0;
    const char* err = expr_eval(p, &align);
    if (err != NULL) {
        return err;
    }
    if (align <= 0 || (align & (align - 1)) != 0) {
        return pr_msg(p, "alignment is not a power of 2");
    }

    /* To the next multiple of align, as an address. This used to pad up to
     * the absolute buffer offset named by the operand, so ".align 64" after
     * more than 64 bytes did nothing at all. */
    const int rem = p->addr_ % (int) align;
    if (rem != 0) {
        pr_skip(p, (int) align - rem);
    }

    return NULL;
}

/* Emits the contents of a double-quoted string, the opening quote already
 * consumed. */
static const char* parse_quoted(parser* p) {
    char buf[512];
    const int n = lex_string(&p->lex_, buf, sizeof(buf));
    if (n == -1) {
        return pr_msg(p, "expected quote");
    }
    if (n == -2) {
        return pr_msg(p, "wrong escape");
    }
    if (n < 0) {
        return pr_msg(p, "string too long");
    }

    for (int i = 0; i < n; i++) {
        if (!pr_wbyte(p, (uint8_t) buf[i])) {
            return pr_msg(p, "output too large");
        }
    }

    return NULL;
}


static const char* emit_expr(parser* p, int width);

static const char* parse_db(parser* p) {
    next(p);
    if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == NONE) {
        return pr_msg(p, "missing argument");
    }

    while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
        if (p->tk_.tk_ == D_QUOTE) {
            const char* err = parse_quoted(p);
            if (err != NULL) {
                return err;
            }
            next(p);
        } else {
            /* Every element is a full expression, so db 'a'+1 and
             * db 128+127-255 work the way the reference reads them. Out of
             * range truncates rather than failing: the reference warns
             * ("Value truncated to 8 bit") and emits the low byte, so
             * refusing would diverge on a source it accepts. */
            const char* err = emit_expr(p, 1);
            if (err != NULL) {
                return err;
            }
        }

        // Either a comma, because there is more to process, or the end of the line.
        if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == NONE) {
            return NULL;
        }
        if (p->tk_.tk_ != COMMA) {
            return pr_msg(p, "expected a comma");
        }
        next(p);
        if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == NONE) {
            return pr_msg(p, "missing argument");
        }
    }

    return NULL;
}

/* "name: equ <expr>" redefines the label on the same line to the expression's
 * value instead of the address it was given when it was read. */
/* .cpu selects the instruction set, and brings an ADL default with it. */
static const char* parse_cpu(parser* p) {
    next(p);
    if (p->tk_.tk_ != NAME && p->tk_.tk_ != NUMBER) {
        return pr_msg(p, "expected a cpu name");
    }

    const char* n = p->tk_.txt_;
    const int sz = p->tk_.sz_;
    char up[8];
    if (sz > 6) {
        return pr_msg(p, "unsupported cpu");
    }
    for (int i = 0; i < sz; i++) {
        up[i] = (n[i] >= 'a' && n[i] <= 'z') ? (char) (n[i] - 32) : n[i];
    }
    up[sz] = 0;

    if (sz == 3 && up[0] == 'Z' && up[1] == '8' && up[2] == '0') {
        p->cpu_ = CPU_Z80;
        p->adl_ = false;
    } else if (sz == 4 && up[0] == 'Z' && up[1] == '1' && up[2] == '8' && up[3] == '0') {
        p->cpu_ = CPU_Z180;
        p->adl_ = false;
    } else if (sz == 4 && up[0] == 'E' && up[1] == 'Z' && up[2] == '8' && up[3] == '0') {
        p->cpu_ = CPU_EZ80;
        p->adl_ = true;
    } else {
        return pr_msg(p, "unsupported cpu");
    }
    next(p);

    return NULL;
}

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

/* Reads a double-quoted string into buf. The lexer breaks a string into
 * whatever tokens its characters happen to form, so this reassembles the raw
 * text -- which is what a filename needs. */
static const char* read_string(parser* p, char* buf, int max, int* out_sz) {
    if (next(p).tk_ != D_QUOTE) {
        return pr_msg(p, "expected a quoted string");
    }

    const int n = lex_string(&p->lex_, buf, max - 1);
    if (n < 0) {
        return pr_msg(p, "bad string");
    }
    buf[n] = 0;
    *out_sz = n;
    next(p);

    return NULL;
}

/* Reads a macro definition: the name, the argument names, then the body text
 * up to endmacro. The body is kept verbatim -- expansion substitutes into it
 * before it is lexed. */
/* Whether a token came from an identifier run, whatever the reserved-word
 * tables made of it. A macro may be named anything spelled that way -- "m" is
 * a real macro name in the reference's corpus, and the lexer hands it back as
 * the M condition code. */
static bool ident_like(TOKEN tk) {
    return tk == NAME || tk == INSTRUCTION || tk == DIRECTIVE
        || tk == REGISTER || tk == FLAG;
}

static const char* parse_macro(parser* p) {
    next(p);
    if (!ident_like(p->tk_.tk_)) {
        return pr_msg(p, "expected a macro name");
    }

    char name[MAX_NAME + 1];
    int name_sz = p->tk_.sz_;
    if (name_sz > MAX_NAME) {
        return pr_msg(p, "name too long");
    }
    for (int i = 0; i < name_sz; i++) {
        name[i] = p->tk_.txt_[i];
    }

    char args[MACRO_MAX_ARGS][MAX_NAME + 1];
    int arg_sz[MACRO_MAX_ARGS];
    int argc = 0;

    next(p);
    while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
        if (p->tk_.tk_ == COMMA) {
            next(p);
            continue;
        }
        /* The name has to be an identifier: a number or a reserved word
         * cannot be substituted for, so "macro test 1" and "macro test and"
         * are both rejected -- as they are by the reference. */
        if (p->tk_.tk_ != NAME || p->tk_.sz_ <= 0 || p->tk_.sz_ > MAX_NAME) {
            return pr_msg(p, "bad macro argument name");
        }
        if (argc == MACRO_MAX_ARGS) {
            return pr_msg(p, "too many macro arguments");
        }
        for (int i = 0; i < p->tk_.sz_; i++) {
            args[argc][i] = p->tk_.txt_[i];
        }
        arg_sz[argc] = p->tk_.sz_;
        argc++;
        next(p);
    }

    char* body = (char*) malloc(4096);
    if (body == NULL) {
        return pr_msg(p, "out of memory");
    }
    const int n = lex_capture(&p->lex_, "endmacro", body, 4096);
    if (n < 0) {
        free(body);

        return pr_msg(p, n == -1 ? "macro without endmacro" : "macro body too long");
    }

    macro* m = mt_add(&p->macros_, name, name_sz, body, n);
    if (m == NULL) {
        free(body);

        return pr_msg(p, "duplicate or too many macros");
    }
    for (int i = 0; i < argc; i++) {
        for (int k = 0; k < arg_sz[i]; k++) {
            m->args[i][k] = args[i][k];
        }
        m->arg_sz[i] = arg_sz[i];
    }
    m->argc = argc;

    return NULL;
}

/* Expands a macro in place, as if its text had been written at the call. */
static const char* expand_macro(parser* p, const macro* m) {
    char argv[MACRO_MAX_ARGS][MACRO_ARG_MAX];
    int argl[MACRO_MAX_ARGS];
    int argc = 0;

    /* Arguments are taken as raw text, so a register or an expression can be
     * passed as readily as a number. */
    next(p);
    while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
        if (p->tk_.tk_ == COMMA) {
            next(p);
            continue;
        }
        if (argc == MACRO_MAX_ARGS) {
            return pr_msg(p, "too many macro arguments");
        }

        int n = 0;
        /* An argument runs to the next comma; a negative number and a
         * bracketed expression both arrive as several tokens. */
        while (p->tk_.tk_ != COMMA && p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
            for (int i = 0; i < p->tk_.sz_; i++) {
                /* Too long is reported rather than trimmed: a truncated
                 * argument expands into something that still looks like
                 * source, so it emits wrong bytes or fails somewhere else
                 * with an error that does not name the real problem. */
                /* The buffer holds MACRO_ARG_MAX characters; the length is
                 * carried separately, so no terminator is needed and all of
                 * them are usable. A quoted 64-character filename is exactly
                 * this long, and the reference allows it. */
                if (n >= MACRO_ARG_MAX) {
                    return pr_msg(p, "macro argument too long");
                }
                argv[argc][n++] = p->tk_.txt_[i];
            }
            next(p);
        }
        argl[argc] = n;
        argc++;
    }

    if (argc != m->argc) {
        return pr_msg(p, "wrong number of macro arguments");
    }

    static char expanded[8192];
    const int n = mt_expand(m, argv, argl, argc, expanded, sizeof(expanded));
    if (n < 0) {
        return pr_msg(p, "macro expansion too long");
    }

    if (p->inc_depth_ == (int) (sizeof(p->inc_) / sizeof(p->inc_[0]))) {
        return pr_msg(p, "macros nested too deeply");
    }

    lexer nested;
    if (br_open_mem(&nested.rd_, expanded, n) == NULL) {
        return pr_msg(p, "out of memory");
    }
    nested.lcount_ = p->lex_.lcount_;
    for (int i = 0; i < (int) sizeof(nested.fname_); i++) {
        nested.fname_[i] = p->lex_.fname_[i];
    }

    p->inc_[p->inc_depth_] = p->lex_;
    p->inc_scope_[p->inc_depth_] = p->scope_;
    p->inc_macro_[p->inc_depth_] = true;
    p->inc_depth_++;
    p->macro_depth_++;
    p->lex_ = nested;

    /* A fresh scope for the expansion, so a local label in the body does not
     * collide with the one from the previous invocation. It is restored when
     * the expansion ends -- a macro call in the middle of a routine must not
     * split that routine's locals in two. */
    if (p->scope_ < MAX_SCOPE) {
        p->scope_++;
    }
    p->expand_id_++;

    return NULL;
}

/* Suspends the current source and reads another in its place. */
static const char* parse_include(parser* p) {
    static char names[8][256];

    if (p->inc_depth_ == (int) (sizeof(p->inc_) / sizeof(p->inc_[0]))) {
        return pr_msg(p, "includes nested too deeply");
    }

    int sz = 0;
    const char* err = read_string(p, names[p->inc_depth_], 256, &sz);
    if (err != NULL) {
        return err;
    }

    p->inc_[p->inc_depth_] = p->lex_;
    /* An include shares the enclosing scope; a macro expansion does not. */
    p->inc_scope_[p->inc_depth_] = p->scope_;
    p->inc_macro_[p->inc_depth_] = false;

    lexer nested;
    if (lex_init(&nested, names[p->inc_depth_]) == NULL) {
        return pr_msg(p, "cannot open include file");
    }
    p->inc_depth_++;
    p->lex_ = nested;

    return NULL;
}

/* Copies a file's bytes straight into the output. */
static const char* parse_incbin(parser* p) {
    char name[256];
    int sz = 0;
    const char* err = read_string(p, name, sizeof(name), &sz);
    if (err != NULL) {
        return err;
    }

    buf_reader rd;
    if (br_open(&rd, name, 4) == NULL) {
        return pr_msg(p, "cannot open binary file");
    }

    for (int ch = br_byte(&rd); ch >= 0; ch = br_byte(&rd)) {
        if (!pr_wbyte(p, (uint8_t) ch)) {
            br_destroy(&rd);

            return pr_msg(p, "output too large");
        }
    }
    br_destroy(&rd);

    return NULL;
}

/* Emits one data value of the given width, deferring it if a name in it is
 * not defined yet. A table of addresses -- "dl level1" with the levels
 * further down -- is the ordinary reason for that. */
static const char* emit_expr(parser* p, int width) {
    char text[128];
    int text_sz = 0;
    value v = 0;

    const char* err = expr_capture(p, &v, text, (int) sizeof(text), &text_sz);
    if (err != NULL) {
        return err;
    }

    if (p->undefined_) {
        const fixup_kind kind = (width == 1) ? FIX_ABS8
                              : (width == 2) ? FIX_ABS16
                              : (width == 3) ? FIX_ABS24 : FIX_ABS32;

        return pr_stack_fixup(p, text, text_sz, kind, -1);
    }

    for (int i = 0; i < width; i++) {
        if (!pr_wbyte(p, (uint8_t) ((v >> (i * 8)) & 0xFF))) {
            return pr_msg(p, "output too large");
        }
    }

    return NULL;
}

/* dw, dl, dw24 and dw32: a list of values, each written little-endian at the
 * given width. Strings are allowed alongside them, as they are for db. */
static const char* parse_data(parser* p, int width) {
    next(p);
    if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == NONE) {
        return pr_msg(p, "missing argument");
    }

    while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
        /* Only db takes a string; dw and the wider forms do not, and the
         * reference refuses them. */
        if (p->tk_.tk_ == D_QUOTE) {
            return pr_msg(p, "string not allowed here");
        }

        const char* err = emit_expr(p, width);
        if (err != NULL) {
            return err;
        }

        if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == NONE) {
            return NULL;
        }
        if (p->tk_.tk_ != COMMA) {
            return pr_msg(p, "expected a comma");
        }
        next(p);
        if (p->tk_.tk_ == NEW_LINE || p->tk_.tk_ == NONE) {
            return pr_msg(p, "missing argument");
        }
    }

    return NULL;
}

/* ds and defs reserve space without writing it. The reference rejects an
 * initialiser here -- "Ignoring unsupported initializer value" -- and fills
 * the gap with the global fill byte only if something lands past it. */
static const char* parse_ds(parser* p) {
    next(p);

    value n = 0;
    const char* err = expr_eval(p, &n);
    if (err != NULL) {
        return err;
    }
    if (n < 0) {
        return pr_msg(p, "negative size");
    }
    /* An initialiser here is read and thrown away. That is not an oversight:
     * the reference warns ("Ignoring unsupported initializer value") and
     * carries on reserving the space uninitialised, so refusing it would
     * reject a source ez80asm accepts. ".ds 10, 0" therefore does not zero
     * anything, in either assembler. */
    if (p->tk_.tk_ == COMMA) {
        next(p);
        value ignored = 0;
        err = expr_eval(p, &ignored);
        if (err != NULL) {
            return err;
        }
    }
    pr_skip(p, (int) n);

    return NULL;
}

/* blkb, blkw, blkp and blkl write their space out, unlike ds. The value is
 * optional and defaults to the global fill byte. */
static const char* parse_blk(parser* p, int width) {
    next(p);

    value n = 0;
    const char* err = expr_eval(p, &n);
    if (err != NULL) {
        return err;
    }
    if (n < 0) {
        return pr_msg(p, "negative size");
    }

    value v = 0;
    bool given = false;
    if (p->tk_.tk_ == COMMA) {
        next(p);
        err = expr_eval(p, &v);
        if (err != NULL) {
            return err;
        }
        given = true;
    }

    for (value i = 0; i < n; i++) {
        for (int b = 0; b < width; b++) {
            const uint8_t byte = given ? (uint8_t) ((v >> (b * 8)) & 0xFF)
                                       : p->fill_;
            if (!pr_wbyte(p, byte)) {
                return pr_msg(p, "output too large");
            }
        }
    }

    return NULL;
}

/* Sets the byte that fills gaps and unvalued blk space. */
static const char* parse_fillbyte(parser* p) {
    next(p);

    value v = 0;
    const char* err = expr_eval(p, &v);
    if (err != NULL) {
        return err;
    }
    p->fill_ = (uint8_t) (v & 0xFF);

    return NULL;
}

static const char* parse_directive(parser* p) {
    switch (p->tk_.tt_) {
        case D_ASSUME:
            return parse_adl(p);
        case D_DW:
        case D_DEFW:
            return parse_data(p, 2);
        case D_DL:
        case D_DW24:
            return parse_data(p, 3);
        case D_DW32:
            return parse_data(p, 4);
        case D_DS:
        case D_DEFS:
            return parse_ds(p);
        case D_BLKB:
            return parse_blk(p, 1);
        case D_BLKW:
            return parse_blk(p, 2);
        case D_BLKP:
            return parse_blk(p, 3);
        case D_BLKL:
            return parse_blk(p, 4);
        case D_FILLBYTE:
            return parse_fillbyte(p);
        case D_RELOCATE: {
            if (p->reloc_) {
                return pr_msg(p, "relocate is already open");
            }

            next(p);
            value v = 0;
            const char* err = expr_eval(p, &v);
            if (err != NULL) {
                return err;
            }
            if (v < 0 || v > 0xFFFFFF) {
                return pr_msg(p, "relocate address out of range");
            }
            p->reloc_ = true;
            p->reloc_base_ = (int) v;
            p->reloc_out_ = p->addr_;

            return NULL;
        }
        case D_ENDRELOCATE:
            if (!p->reloc_) {
                return pr_msg(p, "endrelocate without relocate");
            }
            p->reloc_ = false;
            next(p);

            return NULL;
        case D_MACRO:
            return parse_macro(p);
        case D_IF: {
            if (p->cond_depth_ == (int) (sizeof(p->taken_) / sizeof(p->taken_[0]))) {
                return pr_msg(p, "conditionals nested too deeply");
            }
            next(p);
            value v = 0;
            const char* err = expr_eval(p, &v);
            if (err != NULL) {
                return err;
            }
            p->taken_[p->cond_depth_] = v != 0;
            p->cond_depth_++;
            if (v == 0 && p->skip_depth_ == 0) {
                p->skip_depth_ = p->cond_depth_;
            }

            return NULL;
        }
        case D_ELSE:
            if (p->cond_depth_ == 0) {
                return pr_msg(p, "else without if");
            }
            /* Only this conditional's own else may change the skip state. If
             * an enclosing branch is already being skipped -- skip_depth_ is
             * shallower than this one -- the else must leave it alone, or
             * "if 0 / if 0 / .. / else / .. / endif / endif" would assemble
             * the inner else branch from inside a false outer branch. */
            if (p->skip_depth_ == 0 || p->skip_depth_ == p->cond_depth_) {
                p->skip_depth_ = p->taken_[p->cond_depth_ - 1] ? p->cond_depth_ : 0;
            }
            next(p);

            return NULL;
        case D_ENDIF:
            if (p->cond_depth_ == 0) {
                return pr_msg(p, "endif without if");
            }
            p->cond_depth_--;
            if (p->skip_depth_ > p->cond_depth_) {
                p->skip_depth_ = 0;
            }
            next(p);

            return NULL;
        case D_INCLUDE:
            return parse_include(p);
        case D_INCBIN:
            return parse_incbin(p);
        case D_CPU:
            return parse_cpu(p);
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
            /* ascii and byte are spellings of db in the reference -- they
             * take the same comma-separated list of values and strings, not
             * just one string. */
            return parse_db(p);
        case D_ASCIZ:
            /* db, then a terminating zero. */
            {
                const char* err = parse_db(p);
                if (err != NULL) {
                    return err;
                }
                if (!pr_wbyte(p, 0)) {
                    return pr_msg(p, "output too large");
                }
            }

            return NULL;
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

static const char* parse_label(parser* p) {
    if (!p->tk_.label_) {
        return pr_msg(p, "expected a colon");
    }

    /* Inside a macro body only local labels are allowed. A global or
     * anonymous one would be defined again on every invocation, so the
     * reference refuses them and so does this. */
    if (p->macro_depth_ > 0) {
        const bool local = p->tk_.sz_ > 1 && p->tk_.txt_[0] == '@'
                        && p->tk_.txt_[1] != '@';
        if (!local) {
            return pr_msg(p, "a macro may only define local labels");
        }
    }

    /* An anonymous label has no name to look up -- it is found by position. */
    if (p->tk_.sz_ == 2 && p->tk_.txt_[0] == '@' && p->tk_.txt_[1] == '@') {
        if (p->anon_count_ == (int) (sizeof(p->anon_) / sizeof(p->anon_[0]))) {
            return pr_msg(p, "too many anonymous labels");
        }
        p->anon_[p->anon_count_++] = pr_addr(p);

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
        if (p->scope_ == MAX_SCOPE) {
            return pr_msg(p, "too many labels");
        }
        p->scope_++;
    }

    /* A name too long for the table used to be dropped silently, so the label
     * simply did not exist and every reference to it failed later with no
     * hint why. */
    if (!ht_nset(&p->labels_, key, ksz, pr_addr(p))) {
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
/* Evaluates a deferred expression with every symbol now known, by lexing its
 * stored text as a memory source. */
static const char* resolve_fixup(parser* p, const label_node* ln, value* out) {
    lexer saved = p->lex_;
    const uint16_t saved_scope = p->scope_;
    const int saved_stmt = p->stmt_addr_;
    const bool saved_comment = p->comment_;
    const bool saved_undef = p->undefined_;

    lexer tmp;
    if (br_open_mem(&tmp.rd_, ls_text(&p->ls_, ln), ln->text_len_) == NULL) {
        return pr_msg(p, "out of memory");
    }
    tmp.lcount_ = ln->line_;

    p->lex_ = tmp;
    /* The names were written in that scope, and '$' meant that address. */
    p->scope_ = ln->scope_;
    p->stmt_addr_ = ln->here_;
    p->comment_ = false;
    p->undefined_ = false;

    next(p);
    const char* err = expr_eval(p, out);
    if (err == NULL && p->undefined_) {
        err = pr_msg(p, "label does not exist.");
    }

    br_destroy(&p->lex_.rd_);
    p->lex_ = saved;
    p->scope_ = saved_scope;
    p->stmt_addr_ = saved_stmt;
    p->comment_ = saved_comment;
    p->undefined_ = saved_undef;

    return err;
}

/* Fills in every reference that was still unresolved when its line was read.
 *
 * The old version compared p->pos_-1 against 0x18 -- a buffer offset against
 * the JR opcode -- to decide relative from absolute, and then appended through
 * pr_wbyte instead of writing at the hole, so a forward JR overwrote whatever
 * followed it. Both are recorded on the fixup now. */
static const char* post_process(parser* p) {
    for (const label_node* ln = ls_pop(&p->ls_); ln != NULL; ln = ls_pop(&p->ls_)) {
        int v;
        if (ln->anon_ >= 0) {
            if (ln->anon_ >= p->anon_count_) {
                p->lex_.lcount_ = ln->line_;
            p->stmt_line_ = ln->line_;

                return pr_msg(p, "no anonymous label after here");
            }
            v = p->anon_[ln->anon_];
        } else {
            value ev = 0;
            const char* err = resolve_fixup(p, ln, &ev);
            if (err != NULL) {
                p->lex_.lcount_ = ln->line_;
            p->stmt_line_ = ln->line_;

                return err;
            }
            v = (int) ev;
        }

        if (ln->kind_ == FIX_REL8) {
            const int d = v - ln->next_;
            if (d < -128 || d > 127) {
                p->lex_.lcount_ = ln->line_;
            p->stmt_line_ = ln->line_;

                return pr_msg(p, "relative jump too far");
            }
            p->buf_[ln->bpos_] = (uint8_t) (d & 0xFF);
            continue;
        }

        const int width = (ln->kind_ == FIX_ABS8) ? 1
                        : (ln->kind_ == FIX_ABS16) ? 2
                        : (ln->kind_ == FIX_ABS24) ? 3 : 4;
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
 * used before the line that defines it. A definition it cannot fold yet -- one
 * that depends on a label's address -- is skipped and left to the main pass,
 * where a label reference is deferred anyway.
 *
 * Failures are deliberately silent. Anything genuinely wrong is reported by
 * the real pass, with the right line number and without this one having to
 * guess whether a name it has not reached yet is a mistake. */
static void pr_prescan(parser* p) {
    if (p->fname_ == NULL && p->mem_ == NULL) {
        return;
    }

    lexer saved_lex = p->lex_;
    const uint16_t saved_scope = p->scope_;
    const int saved_pos = p->pos_;
    const bool saved_comment = p->comment_;
    const bool saved_ws = p->skip_ws_;
    const int saved_depth = p->inc_depth_;

    if (p->fname_ != NULL) {
        if (lex_init(&p->lex_, p->fname_) == NULL) {
            p->lex_ = saved_lex;

            return;
        }
    } else {
        if (br_open_mem(&p->lex_.rd_, p->mem_, p->mem_len_) == NULL) {
            p->lex_ = saved_lex;

            return;
        }
        p->lex_.lcount_ = 1;
    }
    p->scope_ = 0;
    p->pos_ = 0;
    p->inc_depth_ = 0;

    token tk = next(p);
    while (tk.tk_ != NONE) {
        /* Follow includes. A constant defined in an included file has to be
         * visible to the same forward uses as one defined here. */
        if (tk.tk_ == DOT) {
            tk = next(p);
        }
        if (tk.tk_ == DIRECTIVE && tk.tt_ == D_INCLUDE) {
            if (parse_include(p) != NULL) {
                while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
                    next(p);
                }
            }
            tk = next(p);
            continue;
        }

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
            if (global && p->scope_ < MAX_SCOPE) {
                p->scope_++;
            }

            /* Both halves matter: tt_ on an INSTRUCTION token is an index
             * into the ISA table, and index 19 has the same numeric value as
             * D_EQU. Without the tk_ check, a label followed by that mnemonic
             * was harvested as a constant and given a garbage value, which
             * then resolved jumps to nonsense. */
            const token after = next(p);
            if (after.tk_ == DIRECTIVE && after.tt_ == D_EQU) {
                next(p);
                value v = 0;
                p->undefined_ = false;
                /* An equ whose value is '$' cannot be folded here: the
                 * prescan has no program counter, so it would store the
                 * wrong number and any use before the definition would pick
                 * it up. "FNCHK: EQU $" in BBC BASIC is exactly that. Left
                 * undefined, it defers and resolves correctly instead. */
                if (expr_eval(p, &v) == NULL && !p->undefined_ && !p->pc_used_) {
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

    /* Close anything an include left open, then put the real source back. */
    while (p->inc_depth_ > 0) {
        br_destroy(&p->lex_.rd_);
        p->lex_ = p->inc_[--p->inc_depth_];
    }
    br_destroy(&p->lex_.rd_);

    p->lex_ = saved_lex;
    p->scope_ = saved_scope;
    p->pos_ = saved_pos;
    p->inc_depth_ = saved_depth;
    p->undefined_ = false;

    /* The comment flag has to come back too. A source whose last line ends
     * inside a comment with no trailing newline left it set, and the real
     * pass then treated the whole file as commented out -- assembling to
     * nothing, with no error at all. */
    p->comment_ = saved_comment;
    p->skip_ws_ = saved_ws;
}

const char* pr_parse(parser* p) {
    // top level parser. On every iteration we are at the beginning of a new line.
    pr_prescan(p);

    p->pos_ = 0;
    p->addr_ = p->org_;
    p->scope_ = 0;
    p->anon_count_ = 0;
    p->comment_ = false;
    p->skip_ws_ = true;
    const char* err = NULL;

    for (p->tk_ = next(p); p->tk_.tk_ != NONE; p->tk_ = next(p)) {
        p->stmt_addr_ = pr_addr(p);
        p->stmt_line_ = p->lex_.lcount_;

        /* Inside a false branch only the conditional directives themselves
         * are read; everything else is passed over. A nested .if still has to
         * be counted, or its .endif would close the outer one. */
        if (p->skip_depth_ > 0) {
            TK_TYPE tt = p->tk_.tt_;
            if (p->tk_.tk_ == DOT) {
                next(p);
                tt = p->tk_.tt_;
            }
            if (p->tk_.tk_ == DIRECTIVE
                && (tt == D_IF || tt == D_ELSE || tt == D_ENDIF)) {
                err = parse_directive(p);
                if (err != NULL) {
                    return err;
                }
            }
            while (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
                next(p);
            }
            continue;
        }

        /* A macro invocation is any identifier-shaped token that names one,
         * checked before the reserved-word meaning: a macro may be called
         * "m", which the lexer reads as a condition code. A name with a colon
         * after it is a label being defined, not an invocation. */
        if (!p->tk_.label_ && ident_like(p->tk_.tk_)) {
            const macro* m = mt_find(&p->macros_, p->tk_.txt_, p->tk_.sz_);
            if (m != NULL) {
                err = expand_macro(p, m);
                if (err != NULL) {
                    return err;
                }
                continue;
            }
        }

        switch (p->tk_.tk_) {
            case DOT:
                err = parse_start_dot(p);
                break;
            case DIRECTIVE:
                err = parse_directive(p);
                break;
            case INSTRUCTION:
                err = enc_instruction(p);
                break;
            case NAME:
                err = parse_label(p);
                break;
            case NUMBER:
                /* A number with a colon after it is someone naming a label 5
                 * or ffh. The reference calls that an invalid label, and
                 * saying so is more use than the "expected a new line" this
                 * would otherwise fall through to. */
                if (p->tk_.label_) {
                    return pr_msg(p, "invalid label");
                }
                break;
            case NEW_LINE:
                p->last_label_sz_ = 0;
                continue;
            default:
                break;
        }
        if (err != NULL) {
            return err;
        }

        // A colon means a label was just defined and the line continues, so
        // last_label_ stays live for an equ that follows it.
        if (p->tk_.tk_ == COLON) {
            continue;
        }

        if (p->tk_.tk_ != NEW_LINE) {
            p->tk_ = next(p);
            /* End of file ends the last line just as well as a newline does.
             * A source whose final line had no trailing newline used to be
             * rejected outright -- and files in the reference corpus are
             * written that way. */
            if (p->tk_.tk_ != NEW_LINE && p->tk_.tk_ != NONE) {
                return pr_msg(p, "expected a new line.");
            }
        }

        /* The line is over, so a label on it no longer stands as the target
         * of a later equ. Without this, "foo: ld a,b" followed by a bare
         * "equ 5" silently redefined foo. */
        p->last_label_sz_ = 0;
    }

    if (p->cond_depth_ != 0) {
        return pr_msg(p, "if without endif");
    }

    return post_process(p);
}

