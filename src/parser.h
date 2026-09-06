#ifndef _PARSER_H_
#define _PARSER_H_

#include <stdint.h>

#include "hash_table.h"
#include "macro.h"
#include "zap.h"
#include "label_stack.h"
#include "lexer.h"

/* How many sources may be suspended by nested .includes at once. Each one
 * keeps its lexer buffer while suspended, so this bounds memory as well as
 * nesting -- see LEX_BUF_KB. */
#define MAX_INCLUDE_DEPTH 8

typedef struct _parser {
    lexer lex_;
    uint8_t* buf_;
    int sz_;
    int pos_;

    int org_;

    /* The address the next emitted byte will live at. Kept apart from pos_,
     * which is the offset into the output buffer: .ORG moves one and not the
     * other, and labels record this one. It used to be derived as
     * pos_ + org_ at the moment a label was resolved, so any .ORG after code
     * silently moved every label defined before it. */
    int addr_;

    /* The address the output buffer starts at, and how far into it anything
     * has actually been written. Between them these give the sparse model the
     * reference uses: .org, ds and align move the address without writing, and
     * the gap they leave is filled only when a later byte lands past it. A
     * gap at the end of the file is not written at all. */
    int start_;
    int high_;

    /* What fills a gap. 0xFF matches the reference's default (FILLBYTE). */
    uint8_t fill_;

    /* .relocate makes labels and '$' report the addresses the code will run
     * at, while the bytes keep landing where they are being written. */
    bool reloc_;
    int reloc_base_;
    int reloc_out_;

    /* Which local scope names are being read in. Bumped at every global
     * label, so the @loop in one routine is a different symbol from the @loop
     * in the next without either having to be renamed. */
    uint16_t scope_;

    /* The largest scope handed out so far, so none is ever handed out twice.
     *
     * scope_ alone is not enough. A global label bumps it and never puts it
     * back, so it climbs; a macro expansion bumps it and *restores* it when
     * the expansion ends, so the next expansion of the same macro was given
     * the same number and its local labels collided with the previous one's.
     * The first expansion was right and every later one resolved its own
     * forward @label to the earlier expansion's -- `jr @over_str` in a puts
     * macro jumped backwards.
     *
     * Reset per pass along with scope_, so both passes hand out the same
     * numbers in the same order. */
    uint16_t scope_hwm_;

    /* Address of the statement being assembled, which is what '$' means.
     * Not addr_: by the time an operand is read the opcode has already been
     * emitted, and "jp $" has to jump to the jp, not to its own operand. */
    int stmt_addr_;

    /* The line the statement being assembled started on. An error is often
     * only detected after the newline has been read -- "ld a," fails when the
     * operand turns out to be missing, by which point the line counter has
     * moved on -- so this is what a diagnostic reports. */
    int stmt_line_;

    /* The label most recently defined on this line, already scoped. EQU needs
     * it: "five: equ 5" defines the name to the left of the directive, and by
     * the time the directive is read the token that held it is gone. */
    char last_label_[MAX_NAME + 1];
    int last_label_sz_;

    /* Addresses of the anonymous labels (@@) in source order. @b and @p mean
     * the one before here, @f and @n the one after, so a backward reference
     * resolves at once and a forward one records the index it is waiting on. */
    int anon_[256];
    int anon_count_;

    /* Sources suspended by an .include, innermost last. next() pops one when
     * the current file runs out, so an include reads as if its text had been
     * written in place. */
    lexer inc_[MAX_INCLUDE_DEPTH];
    int inc_depth_;

    /* The scope to restore when each suspended source resumes. A macro
     * expansion gets its own scope so a local label in the body is fresh each
     * time, but that has to end with the expansion: otherwise the @loop
     * defined before a macro call and the one referenced after it are two
     * different symbols, inside the same routine. An include does not get a
     * new scope, so it restores the one it had. */
    uint16_t inc_scope_[MAX_INCLUDE_DEPTH];

    /* Which suspended sources are macro expansions rather than includes. A
     * macro body may only define local labels: a global or anonymous one
     * would be redefined on every invocation. */
    bool inc_macro_[MAX_INCLUDE_DEPTH];
    int macro_depth_;

    /* Macros, kept as body text. Expansion substitutes into the text and the
     * result is read as a memory source, so an expansion nests exactly like
     * an include does. */
    macro_table macros_;

    /* Bumped per expansion so a local label inside a macro is a fresh symbol
     * each time it is invoked, rather than colliding with the last one. */

    /* Conditional assembly. skip_ ends up non-zero while a false branch is
     * being passed over; the depth is tracked so a nested .if inside a
     * skipped branch does not close the outer one early. */
    int cond_depth_;
    int skip_depth_;
    bool taken_[16];

    /* Set when expression evaluation met a name that is not defined yet. The
     * operand then defers the whole expression instead of failing. */
    bool undefined_;

    /* The scoped-key hash of the first symbol an evaluation found undefined,
     * so a deferred expression can be filed under what it is waiting for.
     * Zero means nothing was missing. */
    uint16_t wait_hash_;

    /* A symbol defined by the statement being parsed, settled once that
     * statement is finished rather than the moment it enters the table.
     *
     * "name: EQU expr" defines the name twice: parse_label enters it as a
     * label at the current address, and the equ then overwrites it with the
     * real value. Settling at the first of those hands every waiting
     * reference the address instead of the constant, which is wrong and looks
     * right -- the output is the same size, with a few values off. */
    uint16_t def_hash_;

    /* Set when an expression read '$'. Its value depends on where the
     * statement sits, so the constant prescan -- which has no meaningful
     * program counter -- must not fold it. */
    bool pc_used_;

    /* Where the error was, kept as data so a caller can put it against the
     * right line rather than parse it back out of a string. */
    zap_diag diag_;
    bool has_diag_;

    bool adl_;

    /* Which instruction rows are usable, set by the .cpu directive. zap only
     * targets the eZ80, but the setting still has to be honoured: it is what
     * enables the undocumented Z80 opcodes the reference gates behind it, and
     * it carries an ADL default with it. */
    uint8_t cpu_;

    token tk_;

    hash_table labels_;
    struct _label_stack ls_;
} parser;

/* Shared with the instruction and expression parsers. */
/* Inline: it is called once per token -- 108,445 times on BBC BASIC -- and
 * what it does around lex_next is one branch. */
static inline void next(parser* p) {
    lex_next(&p->lex_, &p->tk_);

    /* The only thing left to handle is the end of an included file: go back
     * to the one that included it and keep reading, so the include reads as
     * if its text had been written in place. */
    if (p->tk_.tk_ == NONE && p->inc_depth_ > 0) {
        br_destroy(&p->lex_.rd_);
        --p->inc_depth_;
        p->lex_ = p->inc_[p->inc_depth_];
        p->scope_ = p->inc_scope_[p->inc_depth_];
        if (p->inc_macro_[p->inc_depth_]) {
            p->macro_depth_--;
        }

        /* The end of an included file ends the line as well. Its last line
         * often has no trailing newline, and without this it ran on into the
         * line after the .include. */
        p->tk_.tk_ = NEW_LINE;
        p->tk_.txt_ = NULL;   /* synthesised: there is no source text for it */
        p->tk_.sz_ = 0;
        p->tk_.val_ = 0;
        p->tk_.label_ = false;
    }
}
const char* pr_msg(parser* p, const char* msg);
bool pr_wbyte(parser* p, uint8_t b);

/* Writes a run of bytes at the current address, as pr_wbyte would one at a
 * time. Used by .incbin. */
bool pr_wblock(parser* p, const uint8_t* src, int n);
value tk2i(token tk);
const char* pr_stack_fixup(parser* p, const char* text, int sz,
                           fixup_kind kind, int anon);
/* Where the next byte will be written, so a fixup can be placed over a byte
 * that is written before it is known to need one. */
int pr_pos(parser* p);

/* Records a fixup over an opcode byte already written, for an operand whose
 * value folds into it and is not known yet. */
const char* pr_stack_fold(parser* p, const char* text, int sz, int bpos,
                          int aux);

const char* pr_stack_label(parser* p, char* label, int sz, int anon);
const char* pr_stack_relative_label(parser* p, char* label, int sz, int anon);
/* The address to report for the byte at addr_: the same thing, unless
 * .relocate is in force. */
int pr_addr(const parser* p);

/* Records that an evaluation needs this symbol before it can finish. */
void pr_note_waiting(parser* p, const char* name, int sz);

const char* pr_resolve(parser* p, const char* name, int sz, value* out,
                       bool* known, int* anon);

parser* pr_init(parser* p, const char* fname);

/* Assembles source held in memory rather than read from a file. `name` is
 * what diagnostics call it. */
parser* pr_init_mem(parser* p, const char* text, int len, const char* name);
void pr_destroy(parser* p);

const char* pr_parse(parser* p);
uint8_t* pr_buf(parser* p, int* sz);

/* The same bytes, but the caller takes ownership and the parser gives them up.
 * Trimmed to what was actually written. */
uint8_t* pr_take_buf(parser* p, int* sz);

#endif  // _PARSER_H_
