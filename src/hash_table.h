#ifndef _HASH_TABLE_H
#define _HASH_TABLE_H

#include <stdint.h>

#include "hash_node.h"

typedef struct _hash_table {
    hash_node node[256];
} hash_table;


uint8_t pearson_hash(const char* key);
hash_table* ht_init(hash_table* ht);

#endif  // _HASH_TABLE_H_
