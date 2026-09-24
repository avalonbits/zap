/* Calls the routines in ../bytes.s, which zap assembled. The same file builds
 * with agondev and with acc: see docs/zap-with-agondev.md and
 * docs/zap-with-acc.md. */

#include <stdio.h>

/* The library. C leaves the underscore off: fill() is _fill in bytes.s. */
extern void fill(void* dst, unsigned char value, int len);
extern int sum_bytes(const unsigned char* p, int len);
extern int apply_twice(int (*fn)(int), int x);
extern const char greeting[];
extern int apply_calls;

static int triple(int x) {
    return x * 3;
}

int main(void) {
    unsigned char buf[10];
    int ok = 1;

    printf("%s\r\n", greeting);

    fill(buf, 7, sizeof buf);
    int sum = sum_bytes(buf, sizeof buf);
    printf("fill and sum_bytes: %d\r\n", sum);
    ok = ok && sum == 70;

    int x = apply_twice(triple, 5);
    printf("apply_twice(triple, 5): %d\r\n", x);
    ok = ok && x == 45;

    apply_twice(triple, 1);
    printf("apply_twice was called %d times\r\n", apply_calls);
    ok = ok && apply_calls == 2;

    printf(ok ? "all correct\r\n" : "something is wrong\r\n");

    return 0;
}
