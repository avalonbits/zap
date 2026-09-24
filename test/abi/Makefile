# The C half of the calling-convention test. test/abi.sh puts the assembly
# half, assembled by zap, in lib/libasm.a before this runs.
NAME=abi
LIBS=-lasm
LDHAS_ARG_PROCESSING = 0
LDHAS_EXIT_HANDLER = 0

include $(shell agondev-config --makefile)
