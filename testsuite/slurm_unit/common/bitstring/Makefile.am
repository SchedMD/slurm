AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(LIB_SLURM)

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@  #-Wall -ansi -pedantic -std=c99
#MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable

check_PROGRAMS = \
	$(TESTS)

TESTS = bit_unfmt_hexmask-test \
	bitstring-test

bit_unfmt_hexmask_test_CFLAGS = $(MYCFLAGS)
bit_unfmt_hexmask_test_LDADD  = $(LDADD) @CHECK_LIBS@
bitstring_test_CFLAGS = $(MYCFLAGS)
bitstring_test_LDADD  = $(LDADD) @CHECK_LIBS@

endif
