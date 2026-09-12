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

# Every object depends on every header.
#
# agondev's only rule for a C file is `$(OBJDIR)/%.o: $(SRCDIR)/%.c`, with no
# header dependencies and no -MMD, so editing zap.h rebuilds nothing. What that
# produces is worse than a stale binary: it is a *mixed* one. zap_state's
# layout moves, the translation units that were rebuilt read a field at its new
# offset and the ones that were not read it at the old one, and the two halves
# of the assembler disagree about where `org` lives.
#
# It cost a whole afternoon once. zap assembled a two-line file correctly and
# then reported "org goes backwards" for `org $+16`, because `$` was computed
# in a unit that still had the old layout. The host tests could not see it --
# test/run.sh compiles every source every time -- and adding a printf to the
# offending file fixed it by forcing that one unit to rebuild.
#
# Listed as a whole rather than per file: there are seven translation units and
# one shared header, so anything finer would be bookkeeping for no gain.
$(OBJS): $(wildcard $(SRCDIR)/*.h)
