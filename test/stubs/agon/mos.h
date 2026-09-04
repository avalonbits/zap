/* Host-test stubs for the MOS API, mirroring ~/agondev/include/agon/mos.h closely
 * enough that the real sources compile unchanged. The file side is backed by
 * real stdio so the lexer reads actual bytes. */
#ifndef _TEST_STUB_AGON_MOS_H_
#define _TEST_STUB_AGON_MOS_H_

#include <stdint.h>


#define FA_READ           0x01
#define FA_WRITE          0x02
#define FA_CREATE_ALWAYS  0x08
#define FA_OPEN_ALWAYS    0x10

typedef struct { uint32_t objsize; } FFOBJID;
typedef struct { FFOBJID obj; } FIL;

uint8_t  mos_fopen(const char* fname, uint8_t mode);
uint8_t  mos_fclose(uint8_t fh);
uint24_t mos_fread(uint8_t fh, char* buf, uint24_t n);
uint24_t mos_fwrite(uint8_t fh, char* buf, uint24_t n);
uint8_t  mos_flseek(uint8_t fh, uint32_t offset);
FIL*     mos_getfil(uint8_t fh);
void     mos_puts(const char* buf, uint24_t size, char delim);
int      putch(int ch);

#endif  /* _TEST_STUB_AGON_MOS_H_ */
