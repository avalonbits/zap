#ifndef _PARSER_H_
#define _PARSER_H_

#include <stdint.h>

#include "hash_table.h"
#include "label_stack.h"
#include "lexer.h"

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

    /* Which local scope names are being read in. Bumped at every global
     * label, so the @loop in one routine is a different symbol from the @loop
     * in the next without either having to be renamed. */
    uint16_t scope_;

    /* Address of the statement being assembled, which is what '$' means.
     * Not addr_: by the time an operand is read the opcode has already been
     * emitted, and "jp $" has to jump to the jp, not to its own operand. */
    int stmt_addr_;

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

    /* Kept so the constant prescan can re-open the source. */
    const char* fname_;

    bool adl_;

    /* Which instruction rows are usable, set by the .cpu directive. zap only
     * targets the eZ80, but the setting still has to be honoured: it is what
     * enables the undocumented Z80 opcodes the reference gates behind it, and
     * it carries an ADL default with it. */
    uint8_t cpu_;
    bool skip_ws_;
    bool comment_;

    token tk_;

    hash_table labels_;
    struct _label_stack ls_;
} parser;

/* Shared with the instruction and expression parsers. */
token next(parser* p);
const char* pr_msg(parser* p, const char* msg);
bool pr_wbyte(parser* p, uint8_t b);
value tk2i(token tk);
const char* pr_stack_fixup(parser* p, const char* label, int sz,
                           fixup_kind kind, int anon);
const char* pr_stack_label(parser* p, char* label, int sz, int anon);
const char* pr_stack_relative_label(parser* p, char* label, int sz, int anon);
const char* pr_resolve(parser* p, const char* name, int sz, value* out,
                       bool* known, int* anon);

parser* pr_init(parser* p, const char* fname);
void pr_destroy(parser* p);

const char* pr_parse(parser* p);
uint8_t* pr_buf(parser* p, int* sz);

#endif  // _PARSER_H_
