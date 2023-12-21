#ifndef _LABEL_STACK_H_
#define _LABEL_STACK_H_

#include <stdbool.h>

typedef struct _label_node {
    char label_[26];
    int bpos_;
    int line_;
} label_node;

typedef struct _label_stack {
    label_node* nodes_;
    int sz_;
    int pos_;
} label_stack;

label_stack* ls_init(label_stack* ls, int sz);
label_stack* ls_destroy(label_stack* ls);
bool ls_push(label_stack* ls, const char* label, int sz, int bpos, int line);
const label_node* ls_pop(label_stack* ls);

#endif  // _LABEL_STACK_H_
