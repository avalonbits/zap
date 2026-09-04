/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* The zap command. It is a consumer of the library rather than the other way
 * round: everything it does goes through zap.h, which is the same interface an
 * editor uses. */

#include <agon/mos.h>
#include <stdio.h>
#include <string.h>

#include "timing.h"
#include "zap.h"

/* Derives an output name from the input by replacing its extension. */
static void out_name(const char* in, char* out, int max) {
    int n = 0;
    while (in[n] != 0 && in[n] != '.' && n < max - 5) {
        out[n] = in[n];
        n++;
    }
    out[n++] = '.';
    out[n++] = 'b';
    out[n++] = 'i';
    out[n++] = 'n';
    out[n] = 0;
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        printf("Usage: zap <source> [output]\r\n");

        return 0;
    }

    printf("Assembling %s\r\n", argv[1]);

    /* Timed from here to the output being closed, which is the same span the
     * reference reports: it writes its output as it assembles, so its figure
     * covers the write too. */
    const clock_t begin = clock();

    zap_result r;
    if (!zap_assemble_file(argv[1], &r)) {
        if (r.ndiags > 0) {
            printf("%s line %d: %s\r\n", r.diags[0].file, r.diags[0].line,
                   r.diags[0].msg);
        } else {
            printf("Cannot read %s\r\n", argv[1]);
        }
        zap_free(&r);

        return 1;
    }

    char name[64];
    if (argc == 3) {
        int n = 0;
        while (argv[2][n] != 0 && n < (int) sizeof(name) - 1) {
            name[n] = argv[2][n];
            n++;
        }
        name[n] = 0;
    } else {
        out_name(argv[1], name, (int) sizeof(name));
    }

    const uint8_t fh = mos_fopen(name, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        printf("Cannot write %s\r\n", name);
        zap_free(&r);

        return 1;
    }

    /* A short write means the card is full or the device failed. Reporting
     * success would leave a truncated binary looking like a good one. */
    uint24_t wrote = 0;
    if (r.size > 0) {
        wrote = mos_fwrite(fh, (char*) r.bytes, (uint24_t) r.size);
    }
    mos_fclose(fh);

    if (wrote != (uint24_t) r.size) {
        printf("Short write to %s: %u of %d bytes\r\n", name,
               (unsigned) wrote, r.size);
        zap_free(&r);

        return 1;
    }

    const unsigned cs = elapsed_cs(begin, clock());

    printf("Wrote %s, %d bytes\r\n", name, r.size);
    printf("Done in %u.%02u seconds\r\n", cs / 100, cs % 100);
    zap_free(&r);

    return 0;
}
