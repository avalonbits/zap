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

#ifndef _TIMING_H_
#define _TIMING_H_

#include <time.h>

/* Hundredths of a second between two clock readings.
 *
 * CLOCKS_PER_SEC is 100 on the Agon and 1000000 on a host, and both divide by
 * 100 exactly, so this needs no floating point. That matters on the eZ80,
 * where a single %f would pull the whole float formatter into a binary that
 * has no other use for it.
 *
 * Anything shorter than a hundredth reads as zero, which is what the reference
 * does too -- both print "Done in 0.00 seconds" for a file that assembles in
 * four milliseconds.
 *
 * It lives in a header so it can be tested against known clock values rather
 * than against how long a real assembly happens to take, which depends on the
 * machine running the tests. */
static inline unsigned elapsed_cs(clock_t begin, clock_t end) {
    return (unsigned) ((end - begin) / (CLOCKS_PER_SEC / 100));
}

#endif  /* _TIMING_H_ */
