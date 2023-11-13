#ifndef _HASH_TABLE_H
#define _HASH_TABLE_H

#include "hash_node.h"

typedef struct _hash_table {
    hash_node node[256];
} hash_table;

#endif  // _HASH_TABLE_H_
