#ifndef _HASH_TABLE_H_
#define _HASH_TABLE_H_

#include <stdbool.h>
#include <stdint.h>

#include "value.h"

typedef struct _hash_node {
    char key_[MAX_NAME + 1];
    int value_;
    struct _hash_node* next_;
} hash_node;

typedef struct _hash_table {
    hash_node* node_;
    uint24_t sz_;

    /* How many keys are stored. The table doubles when this passes twice the
     * bucket count: an 8-bit hash held it at 256 buckets, and BBC BASIC's 1619
     * symbols meant every lookup walked four nodes on average, with the worst
     * chain fifteen deep. */
    uint24_t count_;

    /* Whether lookups ignore case. The reserved words do -- "LD" and "ld" are
     * the same mnemonic -- but labels do not: the reference treats Foo and foo
     * as two symbols, and real code relies on it, with a constant ENEMY_UP
     * sitting alongside a routine called enemy_up. */
    bool icase_;
} hash_table;

/* A 16-bit Pearson hash: two passes with different starting values, so the
 * table is not capped at the 256 buckets an 8-bit hash allows. */
uint16_t pearson_hash(const char* key, uint8_t sz, bool icase);

/* The same, with the second pass made optional: a table of 256 buckets or
 * fewer cannot use more than eight bits, so it should not pay for them. */
uint16_t pearson_hash_n(const char* key, uint8_t sz, bool icase, bool wide);

hash_table* ht_init(hash_table* ht, int entries, bool icase);
void ht_clear(hash_table* ht);
void ht_destroy(hash_table* ht);

bool ht_set(hash_table* ht, const char* key, int value);
bool ht_nset(hash_table* ht, const char* key, uint8_t ksz, int value);
int  ht_get(hash_table* ht, const char* key, bool* ok);
int  ht_nget(hash_table* ht, const char* key, uint8_t ksz, bool* ok);
void ht_del(hash_table* ht, const char* key);

#endif  // _HASH_TABLE_H_
