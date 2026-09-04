/* Force-included by the host test build (see test/run.sh -include).
 *
 * uint24_t is a builtin type on the eZ80 compiler, not a typedef from any
 * header, so the sources use it without including anything. On the host it has
 * to come from somewhere before the first source line. 32 bits is the smallest
 * host type that holds the eZ80's 24-bit range without truncating. */
#ifndef _TEST_STUB_HOST_TYPES_H_
#define _TEST_STUB_HOST_TYPES_H_

#include <stddef.h>
#include <stdint.h>

typedef int32_t  int24_t;
typedef uint32_t uint24_t;

#endif  /* _TEST_STUB_HOST_TYPES_H_ */
