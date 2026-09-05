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
    ls->free_ = -1;
    ls->live_ = 0;
    for (int i = 0; i < LS_WAIT_BUCKETS; i++) {
        ls->heads_[i] = -1;
    }

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
             int anon, uint16_t wait) {
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

    /* The expression arena is grown first, for both paths. Jumping straight to
     * a reused slot skipped this and wrote the expression past the end of the
     * arena -- heap corruption that only appeared once retirement started
     * handing slots back. */
    while (ls->text_len_ + sz + 1 > ls->text_cap_) {
        const int want = ls->text_cap_ * 2;
        char* grown_text = (char*) realloc(ls->text_, (size_t) want);
        if (grown_text == NULL) {
            return false;
        }
        ls->text_ = grown_text;
        ls->text_cap_ = want;
    }

    /* A slot retired by an earlier definition is reused before the array
     * grows. That is what keeps it small: the array follows how many
     * references are outstanding at once, not how many the file makes. */
    int idx;
    if (ls->free_ >= 0) {
        idx = ls->free_;
        ls->free_ = ls->nodes_[idx].link_;
        goto have_slot;
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

    idx = ls->pos_;
    ls->pos_++;

have_slot: ;
    label_node* n = &ls->nodes_[idx];
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

    /* Link it under what it is waiting for, at the head. */
    const int b = wait % LS_WAIT_BUCKETS;
    n->wait_ = wait;
    n->link_ = ls->heads_[b];
    n->prev_ = -1;
    if (n->link_ >= 0) {
        ls->nodes_[n->link_].prev_ = idx;
    }
    ls->heads_[b] = idx;
    ls->live_++;

    return true;
}

int ls_waiting_on(const label_stack* ls, uint16_t wait) {
    return ls->heads_[wait % LS_WAIT_BUCKETS];
}

int ls_next_waiting(const label_stack* ls, int idx) {
    return ls->nodes_[idx].link_;
}

const label_node* ls_at(const label_stack* ls, int idx) {
    return &ls->nodes_[idx];
}

void ls_retire(label_stack* ls, int idx) {
    label_node* n = &ls->nodes_[idx];

    /* Unlink in constant time, both neighbours known. */
    if (n->prev_ >= 0) {
        ls->nodes_[n->prev_].link_ = n->link_;
    } else {
        ls->heads_[n->wait_ % LS_WAIT_BUCKETS] = n->link_;
    }
    if (n->link_ >= 0) {
        ls->nodes_[n->link_].prev_ = n->prev_;
    }

    /* A free slot is marked by a zero length: a real fixup always has at
     * least one character of expression. */
    n->text_len_ = 0;
    n->link_ = ls->free_;
    ls->free_ = idx;
    ls->live_--;
}

int ls_first_live(const label_stack* ls) {
    return ls_next_live(ls, -1);
}

int ls_next_live(const label_stack* ls, int idx) {
    for (int i = idx + 1; i < ls->pos_; i++) {
        if (ls->nodes_[i].text_len_ != 0) {
            return i;
        }
    }

    return -1;
}

int ls_live_count(const label_stack* ls) {
    return ls->live_;
}

const label_node* ls_pop(label_stack* ls) {
    if (ls->pos_ <= 0) {
        return NULL;
    }
    ls->pos_--;
    const label_node* n = &ls->nodes_[ls->pos_];
    return n;
}

