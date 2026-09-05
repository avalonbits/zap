/* The bucket chain of pending fixups, exercised directly.
 *
 * ls_retire unlinks in constant time using the node's prev_ link. That link is
 * only correct if every push and every retire maintains it on both neighbours,
 * and a mistake there does not crash -- it silently drops the rest of a chain,
 * so fixups waiting on a label are never settled and the output is quietly
 * wrong. The chain is walked after each retirement here to catch that. */

#include <stdio.h>
#include <string.h>

#include "label_stack.h"

static int failures = 0;

static void check(const char* what, bool ok) {
    if (ok) {
        printf("PASS  %s\n", what);
    } else {
        printf("FAIL  %s\n", what);
        failures++;
    }
}

/* Collects the chain hanging off one wait hash, in order. */
static int chain(const label_stack* ls, uint16_t wait, int* out, int max) {
    int n = 0;
    for (int i = ls_waiting_on(ls, wait); i >= 0 && n < max;
         i = ls_next_waiting(ls, i)) {
        out[n++] = i;
    }

    return n;
}

static bool same(const int* got, int got_n, const int* want, int want_n) {
    if (got_n != want_n) {
        return false;
    }

    return memcmp(got, want, (size_t) want_n * sizeof(int)) == 0;
}

static bool push(label_stack* ls, const char* text, uint16_t wait) {
    return ls_push(ls, text, (int) strlen(text), 0, 0, 0, 1, FIX_ABS16, 0, -1,
                   wait);
}

int main(void) {
    /* Nothing is allocated until the first push. A stack that is created and
     * never used has to survive being asked about and destroyed anyway --
     * big.asm takes exactly this path over 473 KB of source. */
    label_stack empty;
    check("init allocates nothing", ls_init(&empty, 1024) != NULL);
    check("empty has nothing live", ls_live_count(&empty) == 0);
    check("empty has no first live", ls_first_live(&empty) == -1);
    check("empty bucket is empty", ls_waiting_on(&empty, 7) == -1);
    ls_destroy(&empty);

    label_stack ls;
    if (ls_init(&ls, 8) == NULL) {
        printf("FAIL  ls_init\n");

        return 1;
    }

    /* Four fixups on one hash. Pushes go to the head, so the chain is the
     * reverse of the push order: 3, 2, 1, 0. */
    const uint16_t h = 7;
    check("push a", push(&ls, "a", h));
    check("push b", push(&ls, "b", h));
    check("push c", push(&ls, "c", h));
    check("push d", push(&ls, "d", h));
    check("four live", ls_live_count(&ls) == 4);

    int got[8];
    const int all[] = {3, 2, 1, 0};
    check("chain is push order reversed",
          same(got, chain(&ls, h, got, 8), all, 4));

    /* From the middle: both neighbours have to be re-linked. Retiring 2 with a
     * stale prev_ on 1 left 1 pointing at a free slot. */
    ls_retire(&ls, 2);
    const int after_mid[] = {3, 1, 0};
    check("retire from the middle keeps the rest",
          same(got, chain(&ls, h, got, 8), after_mid, 3));
    check("three live", ls_live_count(&ls) == 3);

    /* From the head: the bucket head itself moves. */
    ls_retire(&ls, 3);
    const int after_head[] = {1, 0};
    check("retire the head moves the bucket head",
          same(got, chain(&ls, h, got, 8), after_head, 2));

    /* From the tail: nothing follows, so only the predecessor changes. */
    ls_retire(&ls, 0);
    const int after_tail[] = {1};
    check("retire the tail leaves the head intact",
          same(got, chain(&ls, h, got, 8), after_tail, 1));

    /* A reused slot must land back in the chain with a correct prev_. The free
     * list handed slot 0 back, and a push onto the same hash puts it at the
     * head in front of 1. */
    check("push e", push(&ls, "e", h));
    check("two live again", ls_live_count(&ls) == 2);
    const int reused[] = {0, 1};
    check("a reused slot relinks correctly",
          same(got, chain(&ls, h, got, 8), reused, 2));

    /* And retiring the last one empties the bucket rather than leaving a
     * dangling head. */
    ls_retire(&ls, 0);
    ls_retire(&ls, 1);
    check("the bucket empties", ls_waiting_on(&ls, h) < 0);
    check("nothing live", ls_live_count(&ls) == 0);

    /* Two hashes that share a bucket are one chain, and retiring from one must
     * not disturb the other. */
    const uint16_t g = h + LS_WAIT_BUCKETS;
    check("push p", push(&ls, "p", h));
    check("push q", push(&ls, "q", g));
    check("push r", push(&ls, "r", h));
    check("colliding hashes share a bucket",
          ls_waiting_on(&ls, h) == ls_waiting_on(&ls, g));
    const int shared_n = chain(&ls, h, got, 8);
    check("all three are on it", shared_n == 3);
    ls_retire(&ls, got[1]);
    const int rest_n = chain(&ls, h, got, 8);
    check("retiring the middle of a shared bucket keeps two", rest_n == 2);

    ls_destroy(&ls);

    return failures == 0 ? 0 : 1;
}
