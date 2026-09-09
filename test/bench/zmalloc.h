/*
 * An allocation shim that reports what a run actually used.
 *
 * On a machine with 512 KB and no swap, an assembler that is faster and does
 * not fit is not faster, so what a run costs in memory has to be measurable
 * as well as what it costs in time. The symbol table, the name arena and the
 * fixup list all grow with the source rather than being fixed, so the peak
 * depends on what is being assembled.
 *
 * Used by renaming the allocators on the compile line, so nothing in the
 * program has to know:
 *
 *   -DZMALLOC -Dmalloc=z_malloc -Dcalloc=z_calloc \
 *             -Drealloc=z_realloc -Dfree=z_free
 *
 * Every translation unit compiled that way is covered, which includes the
 * shared reader -- so the figure is the whole program and not just the parts
 * that were remembered.
 *
 * The renames are object-like on purpose. A function-like macro would be
 * expanded in stdlib.h's own declaration of malloc and the header would not
 * compile, which is worth knowing before trying it again.
 *
 * Attribution is by a site the caller sets rather than by __FILE__ and
 * __LINE__, for the same reason: carrying them needs a function-like macro.
 * There are few allocations here and naming them is no hardship.
 */
#ifndef _ZMALLOC_H_
#define _ZMALLOC_H_

#include <stddef.h>

/* What the next allocation is for. Set it before allocating; it stays until
 * changed, so a block of related allocations needs one line. */
extern const char* z_site;

void* z_malloc(size_t n);
void* z_calloc(size_t n, size_t sz);
void* z_realloc(void* p, size_t n);
void z_free(void* p);

/* Prints the peak, and what was live at the moment the peak was reached. */
void z_report(void);

#endif  /* _ZMALLOC_H_ */
