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
#include "value.h"

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
            if (n->ksz_ != 0) {
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

    /* The node stores the key's length instead of the terminator that used to
     * follow it, so the cases the terminator was doing work for have to be
     * pinned: a key that fills the field exactly, keys that differ only in
     * length, and the zero length that now marks a free slot. */
    {
        hash_table kt;
        ht_init(&kt, 64, false);

        char longest[MAX_NAME + 1];
        for (int i = 0; i < MAX_NAME; i++) {
            longest[i] = 'k';
        }
        longest[MAX_NAME] = 0;
        check("a key filling the field exactly is stored",
              ht_nset(&kt, longest, MAX_NAME, 4242));
        bool ok = false;
        check("and read back", ht_nget(&kt, longest, MAX_NAME, &ok) == 4242 && ok);

        /* One character shorter is a different key, and with no terminator the
         * length is the only thing that says so. */
        ok = true;
        ht_nget(&kt, longest, MAX_NAME - 1, &ok);
        check("a prefix of it is not the same key", !ok);

        /* In one bucket, so every key is on the same chain and the length is
         * actually consulted. Spread across buckets these never meet, and a
         * lookup that ignored the length would still pass. */
        hash_table one;
        ht_init(&one, 1, false);
        ht_nset(&one, "a", 1, 11);
        ht_nset(&one, "ab", 2, 22);
        ht_nset(&one, "abc", 3, 33);
        ht_nset(&one, "abcd", 4, 44);
        bool a = false;
        bool b = false;
        bool c = false;
        bool d = false;
        check("keys that are prefixes of each other stay distinct on one chain",
              ht_nget(&one, "a", 1, &a) == 11 && ht_nget(&one, "ab", 2, &b) == 22
              && ht_nget(&one, "abc", 3, &c) == 33
              && ht_nget(&one, "abcd", 4, &d) == 44 && a && b && c && d);

        bool absent = true;
        ht_nget(&one, "abcde", 5, &absent);
        check("a longer key than any stored is absent", !absent);
        ht_destroy(&one);

        /* A zero-length key must not match the empty slots it looks like. */
        ok = true;
        ht_nget(&kt, "", 0, &ok);
        check("a zero-length key is never found", !ok);

        /* And the length has to survive a rehash, since growth re-inserts
         * every key from the stored length rather than a terminator. */
        for (int i = 0; i < 400; i++) {
            char k[24];
            const int n = snprintf(k, sizeof(k), "grow_%d", i);
            ht_nset(&kt, k, (uint8_t) n, i);
        }
        ok = false;
        check("the longest key survives growth",
              ht_nget(&kt, longest, MAX_NAME, &ok) == 4242 && ok);
        ht_destroy(&kt);
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
