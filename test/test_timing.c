/*
 * Host tests for the assembly timer.
 *
 * These use known clock values rather than a real assembly. Timing a real one
 * and asserting it took more than a hundredth of a second is a test of the
 * machine running it: the workload that takes two centiseconds under the
 * sanitisers takes none at -O2, so on a faster host correct behaviour would
 * fail. Feeding elapsed_cs fixed numbers tests the conversion itself, which is
 * the part that can be wrong.
 *
 * Everything is written in terms of CLOCKS_PER_SEC, which is 100 on the Agon
 * and 1000000 here.
 */

#include <stdbool.h>
#include <stdio.h>

#include "timing.h"

static int failures = 0;

static void is(const char* name, unsigned got, unsigned want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-44s %u\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-44s got %u, want %u\n", name, got, want);
        failures++;
    }
}

int main(void) {
    const clock_t sec = (clock_t) CLOCKS_PER_SEC;
    const clock_t cs  = (clock_t) (CLOCKS_PER_SEC / 100);

    is("no time at all", elapsed_cs(0, 0), 0);
    is("one centisecond", elapsed_cs(0, cs), 1);
    is("one second is a hundred", elapsed_cs(0, sec), 100);
    is("two seconds", elapsed_cs(0, 2 * sec), 200);
    is("nineteen hundredths", elapsed_cs(0, 19 * cs), 19);

    /* The reading is a difference, so a clock that did not start at zero has
     * to give the same answer. */
    is("measured from a non-zero start", elapsed_cs(sec, 2 * sec), 100);
    is("and from an arbitrary one", elapsed_cs(7 * cs, 26 * cs), 19);

    /* Shorter than a hundredth reads as zero. That is deliberate, and it is
     * what the reference does: both print "Done in 0.00 seconds" for a file
     * that assembles in four milliseconds. */
    is("just under a centisecond", elapsed_cs(0, cs - 1), 0);
    is("just over one", elapsed_cs(0, cs + 1), 1);

    /* The split the CLI prints as "%u.%02u" has to read back as the same
     * number of seconds and hundredths. */
    {
        const unsigned t = elapsed_cs(0, 19 * cs);
        is("printed whole seconds", t / 100, 0);
        is("printed hundredths", t % 100, 19);

        const unsigned u = elapsed_cs(0, 3 * sec + 7 * cs);
        is("printed whole seconds, over a second", u / 100, 3);
        is("printed hundredths, over a second", u % 100, 7);
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
