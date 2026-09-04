#include "hash_table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t pearson_random[256] = {
      1,  87,  49,  12, 176, 178, 102, 166, 121, 193,   6,  84, 249, 230,  44, 163,
     14, 197, 213, 181, 161,  85, 218,  80,  64, 239,  24, 226, 236, 142,  38, 200,
    110, 177, 104, 103, 141, 253, 255,  50,  77, 101,  81,  18,  45,  96,  31, 222,
     25, 107, 190,  70,  86, 237, 240,  34,  72, 242,  20, 214, 244, 227, 149, 235,
     97, 234,  57,  22,  60, 250,  82, 175, 208,   5, 127, 199, 111,  62, 135, 248,
    174, 169, 211,  58,  66, 154, 106, 195, 245, 171,  17, 187, 182, 179,   0, 243,
    132,  56, 148,  75, 128, 133, 158, 100, 130, 126,  91,  13, 153, 246, 216, 219,
    119,  68, 223,  78,  83,  88, 201,  99, 122,  11,  92,  32, 136, 114,  52,  10,
    138,  30,  48, 183, 156,  35,  61,  26, 143,  74, 251,  94, 129, 162,  63, 152,
    170,   7, 115, 167, 241, 206,   3, 150,  55,  59, 151, 220,  90,  53,  23, 131,
    125, 173,  15, 238,  79,  95,  89,  16, 105, 137, 225, 224, 217, 160,  37, 123,
    118,  73,   2, 157,  46, 116,   9, 145, 134, 228, 207, 212, 202, 215,  69, 229,
     27, 188,  67, 124, 168, 252,  42,   4,  29, 108,  21, 247,  19, 205,  39, 203,
    233,  40, 186, 147, 198, 192, 155,  33, 164, 191,  98, 204, 165, 180, 117,  76,
    140,  36, 210, 172,  41,  54, 159,   8, 185, 232, 113, 196, 231,  47, 146, 120,
     51,  65,  28, 144, 254, 221,  93, 189, 194, 139, 112,  43,  71, 109, 184, 209
};

static char upper(const char ch) {
    if (ch >= 0x61 && ch <= 0x7A) {
        return ch - 0x20;
    }
    return ch;
}


uint16_t pearson_hash(const char* key, uint8_t sz, bool icase) {
    return pearson_hash_n(key, sz, icase, true);
}

/* One Pearson pass gives eight bits, which is all a table of 256 buckets or
 * fewer can use. A second pass, seeded differently, gives sixteen -- enough to
 * size the table to the symbol count, at the cost of one more table lookup per
 * character.
 *
 * Only the label table is ever big enough to need it. Paying for it on the
 * reserved-word table, which holds 175 names and never grows, was making
 * every identifier in the source more expensive to reject. */
uint16_t pearson_hash_n(const char* key, uint8_t sz, bool icase, bool wide) {
    uint8_t h1 = 0;

    if (!wide) {
        for (uint8_t i = 0; i < sz; i++) {
            h1 = pearson_random[h1 ^ (uint8_t) (icase ? upper(key[i]) : key[i])];
        }

        return h1;
    }

    uint8_t h2 = 0x5A;
    for (uint8_t i = 0; i < sz; i++) {
        const uint8_t ch = (uint8_t) (icase ? upper(key[i]) : key[i]);
        h1 = pearson_random[h1 ^ ch];
        h2 = pearson_random[h2 ^ ch];
    }

    return (uint16_t) (((uint16_t) h1 << 8) | h2);
}

/* Buckets are a power of two so the index is a mask rather than a modulo --
 * the eZ80 has no divide. */
static uint24_t round_pow2(int n) {
    uint24_t sz = 16;
    while (sz < (uint24_t) n && sz < 8192) {
        sz *= 2;
    }

    return sz;
}

hash_table* ht_init(hash_table* ht, int entries, bool icase) {
    ht->icase_ = icase;
    ht->count_ = 0;
    ht->sz_ = round_pow2(entries);
    ht->node_ = (hash_node*) malloc(ht->sz_ * sizeof(hash_node));
    if (ht->node_ == NULL) {
        return NULL;
    }

    for (uint24_t i = 0; i < ht->sz_; i++) {
        ht->node_[i].next_ = NULL;
        ht->node_[i].key_[0] = 0;
        ht->node_[i].value_ = 0;
    }

    return ht;
}

/* Frees the bucket array and every node chained off it. Buckets themselves
 * live in the array; only the overflow nodes were malloc'd separately. */
void ht_destroy(hash_table* ht) {
    ht->count_ = 0;
    if (ht->node_ == NULL) {
        return;
    }

    for (uint24_t i = 0; i < ht->sz_; i++) {
        hash_node* n = ht->node_[i].next_;
        while (n != NULL) {
            hash_node* next = n->next_;
            free(n);
            n = next;
        }
    }
    free(ht->node_);
    ht->node_ = NULL;
    ht->sz_ = 0;
}

void ht_clear(hash_table* ht) {
    for (uint24_t i = 0; i < ht->sz_; i++) {
        ht->node_[i].key_[0] = 0;
        for (hash_node* n = ht->node_[i].next_; n != NULL; n = n->next_) {
            n->key_[0] = 0;
        }
    }
}

static bool key_equal(const char* s1, const char* s2, uint8_t ksz, bool icase) {
    uint8_t i = 0;
    for (; i < ksz && s1[i] != 0 && s2[i] != 0; i++) {
        const char ch1 = icase ? upper(s1[i]) : s1[i];
        const char ch2 = icase ? upper(s2[i]) : s2[i];
        if (ch1 != ch2) {
            return false;
        }
    }

    return i == ksz;
}

/* Doubles the bucket array and redistributes what is in it. Overflow nodes are
 * reused rather than reallocated: only the array is replaced. */
static bool ht_grow(hash_table* ht);

static bool ht_grow_impl(hash_table* ht) {
    const uint24_t old_sz = ht->sz_;
    hash_node* old = ht->node_;
    if (old_sz >= 8192) {
        return true;  /* large enough; chains stay short at this size */
    }

    hash_node* fresh = (hash_node*) malloc((size_t) (old_sz * 2) * sizeof(hash_node));
    if (fresh == NULL) {
        return true;  /* carry on with what we have rather than fail a build */
    }
    for (uint24_t i = 0; i < old_sz * 2; i++) {
        fresh[i].next_ = NULL;
        fresh[i].key_[0] = 0;
        fresh[i].value_ = 0;
    }

    ht->node_ = fresh;
    ht->sz_ = old_sz * 2;
    ht->count_ = 0;

    /* Re-insert every key, then release the old array and its chain nodes. */
    for (uint24_t i = 0; i < old_sz; i++) {
        for (hash_node* n = &old[i]; n != NULL; ) {
            hash_node* next = n->next_;
            if (n->key_[0] != 0) {
                ht_nset(ht, n->key_, (uint8_t) strlen(n->key_), n->value_);
            }
            if (n != &old[i]) {
                free(n);
            }
            n = next;
        }
    }
    free(old);

    return true;
}

static bool ht_grow(hash_table* ht) {
    return ht_grow_impl(ht);
}

bool ht_nset(hash_table* ht, const char* key, uint8_t ksz, int value) {
    if (ksz > MAX_NAME || ksz <= 0) {
        return false;
    }

    const int pos = (int) (pearson_hash_n(key, ksz, ht->icase_, ht->sz_ > 256)
                           & (ht->sz_ - 1));
    hash_node* node = &ht->node_[pos];

    // Iteratore through all nodes in the chain until we find the key to update
    // or create a new node.
    do {
        if (node->key_[0] == 0) {
            strncpy(node->key_, key, ksz);
            node->key_[ksz] = 0;
            node->value_ = value;
            ht->count_++;

            return true;
        }

        const uint8_t sz = strlen(node->key_);
        if (sz == ksz && key_equal(node->key_, key, ksz, ht->icase_)) {
            node->value_ = value;
            return true;
        }
        if (node->next_ == NULL) {
            break;
        }
        node = node->next_;
    } while (true);

    // Add a new node.
    hash_node* n = (hash_node*) malloc(sizeof(hash_node));
    if (n == NULL) {
        // out of memory.
        return false;
    }
    strncpy(n->key_, key, ksz);
    n->key_[ksz] = 0;
    n->value_ = value;
    n->next_ = NULL;
    node->next_ = n;
    ht->count_++;

    if (ht->count_ > ht->sz_ * 2) {
        ht_grow(ht);
    }

    return true;
}

bool ht_set(hash_table* ht, const char* key, int value) {
    return ht_nset(ht, key, strlen(key), value);
}

int ht_nget(hash_table* ht, const char* key, uint8_t ksz, bool* ok) {
    if (ok) *ok = false;

    if (ksz > MAX_NAME) {
        return 0;
    }

    const int pos = (int) (pearson_hash_n(key, ksz, ht->icase_, ht->sz_ > 256)
                           & (ht->sz_ - 1));
    hash_node* n = &ht->node_[pos];

    for (; n != NULL; n = n->next_) {
        if (n->key_[0] == 0) {
            continue;
        }

        const uint8_t sz = strlen(n->key_);
        if (sz != ksz || !key_equal(n->key_, key, ksz, ht->icase_)) {
            continue;
        }

        if (ok) *ok = true;
        return n->value_;
    }

    return 0;
}

int ht_get(hash_table* ht, const char* key, bool* ok) {
    return ht_nget(ht, key, strlen(key), ok);
}
