#ifndef _HASH_TABLE_H
#define _HASH_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "hash_node.h"

typedef struct _hash_table {
    hash_node node_[256];
} hash_table;

uint8_t pearson_hash(const char* key);

hash_table* ht_init(hash_table* ht);
void ht_clear(hash_table* ht);

bool ht_set(hash_table* ht, const char* key, int value);
int  ht_get(hash_table* ht, const char* key, bool* ok);
void ht_del(hash_table* ht, const char* key);

#endif  // _HASH_TABLE_H_
