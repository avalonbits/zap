#include <mos_api.h>
#include <stdio.h>

#include "hash_table.h"

int main(void) {
    hash_table ht;
    ht_init(&ht);

    ht_set(&ht, "test", 1234);
    ht_set(&ht, "igor", 5678);
    ht_set(&ht, "igop", 9012);

    printf("%d %d\n", sizeof(ht.node_[0]), sizeof(ht));
    printf("%s: %d\n%s: %d\n%s: %d\n%s: %d\n",
            "test", ht_get(&ht, "test", NULL),
            "igor", ht_get(&ht, "igor", NULL),
            "igop", ht_get(&ht, "igop", NULL),
            "random", ht_get(&ht, "random",  NULL));

    ht_clear(&ht);
    printf("%d %d\n", sizeof(ht.node_[0]), sizeof(ht));
    printf("%s: %d\n%s: %d\n%s: %d\n%s: %d\n",
            "test", ht_get(&ht, "test", NULL),
            "igor", ht_get(&ht, "igor", NULL),
            "igop", ht_get(&ht, "igop", NULL),
            "random", ht_get(&ht, "random",  NULL));


    return 0;
}
