/* See zmalloc.h.
 *
 * Inert unless ZMALLOC is defined, so it can be symlinked into dzap/src
 * beside the other shared sources and cost an ordinary build nothing. */
#ifdef ZMALLOC

/* Before anything is included. The build defines these names on the command
 * line so that every file gets the shim, and this is the one file that must
 * not -- otherwise stdlib declares `z_malloc` and the real allocator has no
 * name left to call. */
#undef malloc
#undef calloc
#undef realloc
#undef free

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zmalloc.h"

#define Z_MAXSITE 32

/* Every block carries its size and where it came from, because free and
 * realloc are told neither. The header is part of the measurement -- a real
 * allocator has one too -- but it is reported separately so the figure can be
 * read either way. */
typedef struct {
    size_t n;
    int site;
} zhdr;

static struct {
    const char* name;
    size_t live;
    size_t at_peak;
    size_t most;      /* the largest this site was, whenever that was */
    long count;
} sites[Z_MAXSITE];

static int nsites;
static size_t live, peak, blocks, peak_blocks;

static int site_of(const char* name) {
    for (int i = 0; i < nsites; i++) {
        if (sites[i].name == name || strcmp(sites[i].name, name) == 0) {
            return i;
        }
    }
    if (nsites == Z_MAXSITE) {
        return Z_MAXSITE - 1;
    }
    sites[nsites].name = name;

    return nsites++;
}

/* Snapshots every site when the total is at its highest, so the breakdown
 * describes one real moment rather than a sum of maxima that never coexisted.
 */
static void note(void) {
    if (live > peak) {
        peak = live;
        peak_blocks = blocks;
        for (int i = 0; i < nsites; i++) {
            sites[i].at_peak = sites[i].live;
        }
    }
}

static void add(int s, size_t n) {
    sites[s].live += n;
    sites[s].count++;
    if (sites[s].live > sites[s].most) {
        sites[s].most = sites[s].live;
    }
    live += n;
    blocks++;
    note();
}

const char* z_site = "unattributed";

void* z_malloc(size_t n) {
    zhdr* h = (zhdr*) malloc(sizeof(zhdr) + n);
    if (h == NULL) {
        return NULL;
    }
    h->n = n;
    h->site = site_of(z_site);
    add(h->site, n);

    return h + 1;
}

void* z_calloc(size_t n, size_t sz) {
    void* p = z_malloc(n * sz);
    if (p != NULL) {
        memset(p, 0, n * sz);
    }

    return p;
}

void* z_realloc(void* p, size_t n) {
    if (p == NULL) {
        return z_malloc(n);
    }
    zhdr* h = ((zhdr*) p) - 1;
    const size_t was = h->n;
    const int s = h->site;

    zhdr* grown = (zhdr*) realloc(h, sizeof(zhdr) + n);
    if (grown == NULL) {
        return NULL;
    }
    grown->n = n;
    sites[s].live -= was;
    live -= was;
    blocks--;
    add(s, n);

    return grown + 1;
}

void z_free(void* p) {
    if (p == NULL) {
        return;
    }
    zhdr* h = ((zhdr*) p) - 1;
    sites[h->site].live -= h->n;
    live -= h->n;
    blocks--;
    free(h);
}

void z_report(void) {
    printf("peak %u bytes in %u blocks\r\n", (unsigned) peak,
           (unsigned) peak_blocks);
    for (int i = 0; i < nsites; i++) {
        if (sites[i].most == 0) {
            continue;
        }
        printf("  %-28s %7u at peak, %7u most, %ld allocs\r\n",
               sites[i].name, (unsigned) sites[i].at_peak,
               (unsigned) sites[i].most, sites[i].count);
    }
    printf("  (%u bytes of that is this shim's own headers)\r\n",
           (unsigned) (peak_blocks * sizeof(zhdr)));
}

#endif  /* ZMALLOC */
