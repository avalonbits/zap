# ----------------------------
# Makefile Options
# ----------------------------

NAME=zap

# Select option for Argument Processing at 'int main( int argc, char* argv[] )'
# 0: Simple Command Line Processing
# 1: Complex Command Line Processing - for Redirection & Quoting
LDHAS_ARG_PROCESSING = 0
LDHAS_EXIT_HANDLER = 0

# ----------------------------
#
include $(shell agondev-config --makefile)

# agondev sets CFLAGS in its own makefile.inc, so the warning flags the CEdev
# build used have to be appended after the include rather than before it.
CFLAGS += -Wall -Wextra

# A hook for a build that measures something, so a measuring build is the
# ordinary build plus flags rather than a different build. Setting CFLAGS on
# the command line instead replaces what agondev's makefile put there, which
# fails in ways that look like the measurement not working.
#
#   make EXTRA_CFLAGS='-DZMALLOC -Dmalloc=z_malloc ...'
#   make EXTRA_CFLAGS=-DTRUNC=4
CFLAGS += $(EXTRA_CFLAGS)
