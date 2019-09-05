AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

check_PROGRAMS = \
	$(TESTS)

TESTS = \
	bitstring-test

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@  #-Wall -ansi -pedantic -std=c99
#MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += bit_unfmt_hexmask-test

bit_unfmt_hexmask_test_CFLAGS = $(MYCFLAGS)
bit_unfmt_hexmask_test_LDADD  = $(LDADD) @CHECK_LIBS@

endif
