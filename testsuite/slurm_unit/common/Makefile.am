AUTOMAKE_OPTIONS = foreign

SUBDIRS = slurm_protocol_pack slurmdb_pack

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

check_PROGRAMS = \
	$(TESTS)

TESTS = \
	bitstring-test \
	job-resources-test \
	log-test \
	pack-test

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall -ansi -pedantic -std=c99
MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += xtree-test \
	 xhash-test
xtree_test_CFLAGS = $(MYCFLAGS)
xtree_test_LDADD  = $(LDADD) @CHECK_LIBS@
xhash_test_CFLAGS = $(MYCFLAGS)
xhash_test_LDADD  = $(LDADD) @CHECK_LIBS@
endif

