# dzap -- direct zap. See src/dzap.c for what it is and why it exists.
#
# Its own project rather than a target in zap's Makefile, because the agondev
# build finds every .c under src and two of them cannot both have main. The
# shared sources are symlinked rather than copied so they cannot drift, and
# they stay separate translation units -- compiling them as one file would let
# the compiler inline across boundaries that zap does not get, which would
# flatter exactly the number this program exists to measure.

NAME=dzap

LDHAS_ARG_PROCESSING = 0
LDHAS_EXIT_HANDLER = 0

include $(shell agondev-config --makefile)

CFLAGS += -Wall -Wextra -I../src

# A hook for a build that measures something, so a measuring build is the
# ordinary build plus flags rather than a different build. Setting CFLAGS on
# the command line instead replaces what agondev's makefile put there, which
# fails in ways that look like the measurement not working.
#
#   make EXTRA_CFLAGS='-DZMALLOC -Dmalloc=z_malloc ...'
CFLAGS += $(EXTRA_CFLAGS)
