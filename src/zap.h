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

#ifndef _ZAP_H_
#define _ZAP_H_

#include <stdbool.h>
#include <stdint.h>

/* zap's public interface.
 *
 * The point of it is assembling from memory: an editor already holds the
 * source, and writing it to a temporary file just to assemble it is both slow
 * on an Agon and wrong when the buffer has unsaved changes. zap_assemble_mem
 * takes the text directly.
 *
 * Errors come back as data rather than a printed line, so a caller can put
 * them where they belong -- against the line in the editor, rather than in a
 * console. */

#define ZAP_MAX_FILE 64
#define ZAP_MAX_MSG  128

/* Where something went wrong. `file` is the source the error was found in,
 * which is not necessarily the one that was handed to zap: a program with
 * includes reports the file the line is actually in. */
typedef struct _zap_diag {
    char file[ZAP_MAX_FILE];
    int line;
    char msg[ZAP_MAX_MSG];
} zap_diag;

/* The result of assembling. `bytes` is owned by the result and released by
 * zap_free.
 *
 * Assembly stops at the first error, so `ndiags` is 0 or 1 today. It is a list
 * because reporting every error in a file needs the parser to recover and
 * carry on, which is a separate piece of work; the shape is here so adding it
 * does not change the interface. */
typedef struct _zap_result {
    bool ok;

    uint8_t* bytes;
    int size;
    int origin;   /* the address the first byte is meant to live at */

    zap_diag diags[8];
    int ndiags;
} zap_result;

/* Assembles a source file. */
bool zap_assemble_file(const char* path, zap_result* out);

/* Assembles source held in memory. `name` is what diagnostics call it, and
 * may be NULL. The text is not modified, and has to stay valid for the
 * duration of the call -- it is read twice, once by the constant prescan and
 * once by the assembly proper -- but need not outlive it.
 *
 * An .include inside it still reads from the filesystem, relative to the
 * working directory. */
bool zap_assemble_mem(const char* text, int len, const char* name,
                      zap_result* out);

/* Releases what a result owns. Safe on a failed result. */
void zap_free(zap_result* out);

#endif  /* _ZAP_H_ */
