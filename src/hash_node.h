#ifndef _HASH_NODE_H_
#define _HASH_NODE_H_

typedef struct _hash_node {
    char key_[32];
    struct _hash_node* next_;
} hash_node;

#endif  // _HASH_NODE_H_
