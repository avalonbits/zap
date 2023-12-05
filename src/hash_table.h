#ifndef _HASH_TABLE_H_
#define _HASH_TABLE_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _hash_node {
    int value_;
    struct _hash_node* next_;
    char key_[26];
} hash_node;

typedef struct _hash_table {
    hash_node* node_;
    uint24_t sz_;
} hash_table;

uint8_t pearson_hash(const char* key, uint8_t sz);

hash_table* ht_init(hash_table* ht, int entries);
void ht_clear(hash_table* ht);

bool ht_set(hash_table* ht, const char* key, int value);
bool ht_nset(hash_table* ht, const char* key, uint8_t ksz, int value);
int  ht_get(hash_table* ht, const char* key, bool* ok);
int  ht_nget(hash_table* ht, const char* key, uint8_t ksz, bool* ok);
void ht_del(hash_table* ht, const char* key);

#endif  // _HASH_TABLE_H_
