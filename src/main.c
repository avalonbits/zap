#include <stdio.h>
#include <mos_api.h>

#include "buf_reader.h"
#include "hash_table.h"

int main(int argc, char** argv) {
    buf_reader br;
    const char* fname = "refresh.txt";
    if (argc > 1) {
        fname = argv[1];
    }
    if (br_open(&br, fname, 1) == NULL) {
        return -1;
    }

    char buf[157];
    while (true) {
        int read = br_read(&br, buf, 157);
        if (read == EOF) {
            break;
        }
        for (int i = 0; i < read; i++) {
            putch(buf[i]);
        }
    }
    br_close(&br);
    return 0;
}
