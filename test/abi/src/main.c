/* Calls every function in test/abi/asm.s, which zap assembled as an object,
 * and checks what comes back. One line per check, PASS or FAIL, and a summary
 * test/abi.sh reads. */

#include <stdio.h>
#include <string.h>

typedef struct {
    int a;
    int b;
} Pair;

extern int asm_add3(int a, int b, int c);
extern char asm_char_sum(char a, char b);
extern short asm_short_sub(short a, short b);
extern long asm_long_add(long a, long b);
extern long long asm_llong_pass(long long a);
extern int asm_after_llong(long long a, int b);
extern void asm_store(int* p, int v);
extern Pair asm_make_pair(int x);
extern int asm_call_c(int a, int b);
extern void asm_bump(void);
extern void asm_clobber(void);
extern unsigned char asm_table_lo(void);
extern unsigned char asm_table_hi(void);
extern unsigned char asm_table_up(void);

extern int asm_table[3];
extern int asm_table_end[1];
extern unsigned short asm_words[2];
extern const char asm_message[];
extern unsigned char asm_buffer[16];

/* Where agondev's linker script starts and ends the bss. */
extern char __low_bss[];
extern char __heapbot[];

int c_counter = 41;

/* Called from asm_call_c: the order of the arguments is the point. */
int c_sub(int a, int b) {
    return a - b;
}

static int failed;

static void check(const char* what, long got, long want) {
    if (got == want) {
        printf("PASS %s\r\n", what);
    } else {
        printf("FAIL %s: got %lx, want %lx\r\n", what, got, want);
        failed++;
    }
}

/* Locals and arguments in an IX frame, live across a call that clobbers
 * every other register. */
static int __attribute__((noinline)) survives(int a, int b, int c) {
    int x = a * 3;
    int y = b + c;
    asm_clobber();

    return x + y + a - c;
}

int main(void) {
    check("three ints in 3-byte slots", asm_add3(0x100000, 0x020000, 0x003456), 0x123456);
    check("two chars, the result in A", asm_char_sum(40, 2), 42);
    check("two shorts, the result in HL", asm_short_sub(1000, 1234), (short) -234);
    check("two longs in 6-byte slots, the result in E:HL",
          asm_long_add(0x12FFFFFFL, 0x01000001L), 0x14000000L);

    long long ll = asm_llong_pass(0x1122334455667788LL);
    check("a long long, low 3 bytes in HL", (long) (ll & 0xFFFFFF), 0x667788L);
    check("a long long, next 3 bytes in DE", (long) ((ll >> 24) & 0xFFFFFF), 0x334455L);
    check("a long long, top 2 bytes in BC", (long) ((ll >> 48) & 0xFFFF), 0x1122L);
    check("an argument after a long long's 9-byte slot",
          asm_after_llong(0x1122334455667788LL, 77), 77);

    int cell = 0;
    asm_store(&cell, 0x0BEEF0);
    check("a pointer argument", cell, 0x0BEEF0);

    Pair p = asm_make_pair(500);
    check("a struct result through the hidden pointer, first field", p.a, 500);
    check("a struct result through the hidden pointer, second field", p.b, 501);

    check("assembly calling C, arguments in order", asm_call_c(100, 30), 71);
    asm_bump();
    check("assembly writing a C global through XREF", c_counter, 42);
    check("IX and the frame survive a call that clobbers the rest",
          survives(10, 20, 3), 30 + 23 + 10 - 3);

    check("data exported with XDEF", asm_table[2], 0x123456);
    check("a label distance in data", asm_table_end[0], 9);
    check("a 16-bit relocation", asm_words[0], (long) ((unsigned long) asm_table & 0xFFFF));
    check("a label distance in a 16-bit field", asm_words[1], 9);
    check("the low byte of an address", asm_table_lo(), (long) ((unsigned long) asm_table & 0xFF));
    check("the high byte of an address", asm_table_hi(), (long) (((unsigned long) asm_table >> 8) & 0xFF));
    check("the upper byte of an address", asm_table_up(), (long) (((unsigned long) asm_table >> 16) & 0xFF));
    check("read-only data exported with XDEF", strcmp(asm_message, "zap"), 0);

    int zero = 1;
    for (int i = 0; i < 16; i++) {
        if (asm_buffer[i] != 0) {
            zero = 0;
        }
    }
    check("the bss starts zeroed", zero, 1);
    asm_buffer[15] = 0x77;
    check("the bss is writable", asm_buffer[15], 0x77);
    check("the buffer is placed in the bss",
          (char*) asm_buffer >= __low_bss && (char*) asm_buffer + 16 <= __heapbot, 1);

    printf("ABI %d FAILED\r\n", failed);

    return 0;
}
