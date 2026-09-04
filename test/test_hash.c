/*
 * Host tests for the symbol table.
 *
 * The table used to be capped at 256 buckets because the hash produced eight
 * bits. BBC BASIC has 1619 symbols, which meant a load factor of 6.3: every
 * lookup walked about four nodes, the worst chain was fifteen deep, and it got
 * worse with every symbol a program added. These check that it grows, that
 * growth does not lose or corrupt anything, and that case sensitivity survives
 * a rehash.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hash_table.h"

static int failures = 0;

static void check(const char* name, bool ok) {
    if (ok) {
        fprintf(stderr, "PASS  %s\n", name);
    } else {
        fprintf(stderr, "FAIL  %s\n", name);
        failures++;
    }
}

/* Mean nodes visited by a successful lookup, which is what a chain costs. */
static double mean_probes(const hash_table* ht) {
    long total = 0;
    long sumsq = 0;
    for (uint24_t i = 0; i < ht->sz_; i++) {
        long len = 0;
        for (const hash_node* n = &ht->node_[i]; n != NULL; n = n->next_) {
            if (n->key_[0] != 0) {
                len++;
            }
        }
        total += len;
        sumsq += len * len;
    }

    return total ? (double) (sumsq + total) / (2.0 * total) : 0.0;
}

int main(void) {
    /* Enough symbols to force several doublings. A real program of this size
     * exists -- this is roughly BBC BASIC's symbol count doubled. */
    enum { N = 3000 };

    hash_table ht;
    check("init", ht_init(&ht, 255, false) != NULL);

    for (int i = 0; i < N; i++) {
        char key[32];
        const int n = snprintf(key, sizeof(key), "symbol_%d", i);
        if (!ht_nset(&ht, key, (uint8_t) n, i)) {
            check("insert", false);

            return 1;
        }
    }

    check("grew past the old 256-bucket cap", ht.sz_ > 256);
    check("counted every key", ht.count_ == (uint24_t) N);

    bool all = true;
    for (int i = 0; i < N && all; i++) {
        char key[32];
        const int n = snprintf(key, sizeof(key), "symbol_%d", i);
        bool ok = false;
        all = ht_nget(&ht, key, (uint8_t) n, &ok) == i && ok;
    }
    check("every key survived the rehashes with its value", all);

    bool absent = true;
    for (int i = N; i < N + 200 && absent; i++) {
        char key[32];
        const int n = snprintf(key, sizeof(key), "symbol_%d", i);
        bool ok = true;
        ht_nget(&ht, key, (uint8_t) n, &ok);
        absent = !ok;
    }
    check("keys that were never inserted are still absent", absent);

    const double probes = mean_probes(&ht);
    fprintf(stderr, "      %u buckets, %u keys, %.2f mean probes\n",
            (unsigned) ht.sz_, (unsigned) ht.count_, probes);
    /* Without growth this would be about 7 at this size. */
    check("chains stayed short", probes < 2.5);

    ht_destroy(&ht);

    /* Case sensitivity has to survive growth, since the hash sees the case
     * too: Foo and foo are two symbols, and a rehash must not merge them. */
    {
        hash_table cs;
        ht_init(&cs, 16, false);
        for (int i = 0; i < 500; i++) {
            char lo[24];
            char up[24];
            const int n = snprintf(lo, sizeof(lo), "name_%d", i);
            snprintf(up, sizeof(up), "NAME_%d", i);
            ht_nset(&cs, lo, (uint8_t) n, i);
            ht_nset(&cs, up, (uint8_t) n, i + 100000);
        }

        bool distinct = true;
        for (int i = 0; i < 500 && distinct; i++) {
            char lo[24];
            char up[24];
            const int n = snprintf(lo, sizeof(lo), "name_%d", i);
            snprintf(up, sizeof(up), "NAME_%d", i);
            bool a = false;
            bool b = false;
            distinct = ht_nget(&cs, lo, (uint8_t) n, &a) == i
                    && ht_nget(&cs, up, (uint8_t) n, &b) == i + 100000
                    && a && b;
        }
        check("case-sensitive keys stay distinct across growth", distinct);
        ht_destroy(&cs);
    }

    /* A case-insensitive table -- the reserved words -- still matches either
     * spelling, and the hash has to agree with the comparison or a lookup
     * lands in the wrong bucket. */
    {
        hash_table ci;
        ht_init(&ci, 64, true);
        ht_nset(&ci, "LD", 2, 7);
        bool a = false;
        bool b = false;
        bool c = false;
        const bool same = ht_nget(&ci, "ld", 2, &a) == 7
                       && ht_nget(&ci, "Ld", 2, &b) == 7
                       && ht_nget(&ci, "LD", 2, &c) == 7;
        check("case-insensitive lookup matches any spelling", same && a && b && c);
        ht_destroy(&ci);
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
