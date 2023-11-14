#include <stdio.h>

#include "hash_table.h"

int main(void) {
    hash_table ht;
    printf("%d %d\n", sizeof(ht.node[0]), sizeof(ht));
    printf("%s: %d\n%s: %d\n%s: %d",
            "test", pearson_hash("test"),
            "igor", pearson_hash("igor"),
            "igop", pearson_hash("igop"));
    return 0;
}
