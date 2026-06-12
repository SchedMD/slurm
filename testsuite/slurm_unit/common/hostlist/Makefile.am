AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)
LDADD = $(LIB_SLURM) -ldl -lpthread

check_PROGRAMS = \
	$(TESTS)

TESTS =

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@  #-Wall -ansi -pedantic -std=c99
#MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += \
	hostlist_nth-test \
	hostlist_iterator-test

hostlist_nth_test_CFLAGS = $(MYCFLAGS)
hostlist_nth_test_LDADD  = $(LDADD) @CHECK_LIBS@
hostlist_iterator_test_CFLAGS = $(MYCFLAGS)
hostlist_iterator_test_LDADD  = $(LDADD) @CHECK_LIBS@

endif
