#include "label_stack.h"

#include <stdlib.h>
#include <string.h>

label_stack* ls_init(label_stack* ls, int sz) {
    ls->nodes_ = (label_node*) malloc(sz * sizeof(label_node));
    if (ls->nodes_ == NULL) {
        return NULL;
    }
    ls->text_cap_ = 4096;
    ls->text_ = (char*) malloc((size_t) ls->text_cap_);
    if (ls->text_ == NULL) {
        free(ls->nodes_);
        ls->nodes_ = NULL;

        return NULL;
    }
    ls->text_len_ = 0;
    ls->sz_ = sz;
    ls->pos_ = 0;
    return ls;
}

void ls_destroy(label_stack* ls) {
    free(ls->nodes_);
    ls->nodes_ = NULL;
    free(ls->text_);
    ls->text_ = NULL;
    ls->text_cap_ = 0;
    ls->text_len_ = 0;
}

const char* ls_text(const label_stack* ls, const label_node* n) {
    return &ls->text_[n->text_off_];
}

bool ls_push(label_stack* ls, const char* text, int sz, int bpos,
             int next, int here, int line, fixup_kind kind, uint16_t scope,
             int anon) {
    /* text_len_ is a byte. An operand's expression is captured into a
     * 128-character buffer so this cannot be exceeded today, but a silent
     * truncation here would corrupt the deferred expression rather than fail,
     * so it is refused instead of assumed. */
    if (sz < 0 || sz > 255) {
        return false;
    }

    if (sz <= 0) {
        return false;
    }

    /* Grows rather than failing at a fixed size. A large program has more
     * forward references than any figure picked in advance: BBC BASIC has
     * over a thousand. */
    if (ls->pos_ == ls->sz_) {
        const int want = ls->sz_ * 2;
        label_node* grown = (label_node*) realloc(ls->nodes_,
                                                  (size_t) want * sizeof(label_node));
        if (grown == NULL) {
            return false;
        }
        ls->nodes_ = grown;
        ls->sz_ = want;
    }

    while (ls->text_len_ + sz + 1 > ls->text_cap_) {
        const int want = ls->text_cap_ * 2;
        char* grown = (char*) realloc(ls->text_, (size_t) want);
        if (grown == NULL) {
            return false;
        }
        ls->text_ = grown;
        ls->text_cap_ = want;
    }

    label_node* n = &ls->nodes_[ls->pos_];
    n->text_off_ = ls->text_len_;
    n->text_len_ = (uint8_t) sz;
    for (int i = 0; i < sz; i++) {
        ls->text_[ls->text_len_ + i] = text[i];
    }
    ls->text_[ls->text_len_ + sz] = 0;
    ls->text_len_ += sz + 1;

    n->bpos_ = bpos;
    n->next_ = next;
    n->here_ = here;
    n->line_ = line;
    n->kind_ = (uint8_t) kind;
    n->scope_ = scope;
    n->anon_ = anon;
    ls->pos_++;

    return true;
}

const label_node* ls_pop(label_stack* ls) {
    if (ls->pos_ <= 0) {
        return NULL;
    }
    ls->pos_--;
    const label_node* n = &ls->nodes_[ls->pos_];
    return n;
}

