#include "hash_table.h"

#include <agon/vdp_vdu.h>
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


uint8_t pearson_hash(const char* key, uint8_t sz) {
    uint8_t h = 0;
    for (uint8_t i = 0; i < sz; i++) {
        const uint8_t ch = (uint8_t) upper(key[i]);
        h = pearson_random[h ^ ch];
    }
    return h;
}

hash_table* ht_init(hash_table* ht, int entries) {
    if (entries >= 128) {
        ht->sz_ = 256;
    } else if (entries >= 64) {
        ht->sz_ = 64;
    } else if (entries >= 32) {
        ht->sz_ = 32;
    } else {
        ht->sz_ = 16;
    }
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

void ht_print(hash_table* ht) {
    for(uint24_t i = 0; i < ht->sz_; i++) {
        bool has = false;
        for (hash_node* n = &ht->node_[i]; n != NULL; n = n->next_) {
            if (strlen(n->key_) == 0) {
                continue;
            }
            has = true;
            putch('|');
            mos_puts(n->key_, 0, 0);
        }
        if (has)   mos_puts("\r\n", 0, 0);
    }
}

void ht_clear(hash_table* ht) {
    for (uint24_t i = 0; i < ht->sz_; i++) {
        ht->node_[i].key_[0] = 0;
        for (hash_node* n = ht->node_[i].next_; n != NULL; n = n->next_) {
            n->key_[0] = 0;
        }
    }
}

bool icase_equal(const char* s1, const char* s2, uint8_t ksz) {
    uint8_t i = 0;
    for (; i < ksz && s1[i] != 0 && s2[i] != 0; i++) {
        char ch1 = upper(s1[i]);
        char ch2 = upper(s2[i]);
        if (ch1 != ch2) {
            return false;
        }
    }
    return i == ksz;
}

bool ht_nset(hash_table* ht, const char* key, uint8_t ksz, int value) {
    // Hash table keys MUST have at most 25 characters.
    if (ksz > 25 || ksz <= 0) {
        return false;
    }

    const int pos = pearson_hash(key, ksz) % ht->sz_;
    hash_node* node = &ht->node_[pos];

    // Iteratore through all nodes in the chain until we find the key to update
    // or create a new node.
    do {
        if (node->key_[0] == 0) {
            strncpy(node->key_, key, ksz);
            node->key_[ksz] = 0;
            node->value_ = value;
            return true;
        }

        const uint8_t sz = strlen(node->key_);
        if (sz == ksz && icase_equal(node->key_, key, ksz)) {
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

    return true;
}

bool ht_set(hash_table* ht, const char* key, int value) {
    return ht_nset(ht, key, strlen(key), value);
}

int ht_nget(hash_table* ht, const char* key, uint8_t ksz, bool* ok) {
    if (ok) *ok = false;

    if (ksz > 25) {
        return 0;
    }

    const int pos = pearson_hash(key, ksz) % ht->sz_;
    hash_node* n = &ht->node_[pos];

    for (; n != NULL; n = n->next_) {
        if (n->key_[0] == 0) {
            continue;
        }

        const uint8_t sz = strlen(n->key_);
        if (sz != ksz || !icase_equal(n->key_, key, ksz)) {
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
