#include "label_stack.h"

#include <stdlib.h>
#include <string.h>

label_stack* ls_init(label_stack* ls, int sz) {
    ls->nodes_ = (label_node*) malloc(sz * sizeof(label_node));
    if (ls->nodes_ == NULL) {
        return NULL;
    }
    ls->sz_ = sz;
    ls->pos_ = 0;
    return ls;
}

label_stack* ls_destroy(label_stack* ls) {
    free(ls->nodes_);
}

bool ls_push(label_stack* ls, const char* label, int sz, int bpos, int line) {
    if (ls->pos_ == ls->sz_) {
        return false;
    }

    label_node* n = &ls->nodes_[ls->pos_];
    strncpy(n->label_, label, sz);
    n->bpos_ = bpos;
    n->line_ = line;
    ls->pos_++;

    return true;
}

char* ls_pop(label_stack* ls) {
    if (ls->pos_ == 0) {
        return NULL;
    }
    return ls->nodes_[--ls->pos_].label_;
}

