#ifndef _HASH_NODE_H_
#define _HASH_NODE_H_

typedef struct _hash_node {
    int value;
    struct _hash_node* next_;
    char key_[26];
} hash_node;

#endif  // _HASH_NODE_H_
