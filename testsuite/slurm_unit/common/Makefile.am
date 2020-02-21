AUTOMAKE_OPTIONS = foreign

SUBDIRS = bitstring slurm_protocol_pack slurmdb_pack

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

check_PROGRAMS = \
	$(TESTS)

TESTS = \
	job-resources-test \
	log-test \
	pack-test

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += xtree-test \
	 xhash-test \
	 data-test \
	 slurm_opt-test \
	 xstring-test \
	 parse_time-test
xtree_test_CFLAGS = $(MYCFLAGS)
xtree_test_LDADD  = $(LDADD) @CHECK_LIBS@
xhash_test_CFLAGS = $(MYCFLAGS)
xhash_test_LDADD  = $(LDADD) @CHECK_LIBS@
data_test_CFLAGS  = $(MYCFLAGS)
data_test_LDADD   = $(LDADD) @CHECK_LIBS@
slurm_opt_test_CFLAGS = $(MYCFLAGS)
slurm_opt_test_LDADD  = $(LDADD) @CHECK_LIBS@
xstring_test_CFLAGS   = $(MYCFLAGS)
xstring_test_LDADD    = $(LDADD) @CHECK_LIBS@
parse_time_test_CFLAGS= $(MYCFLAGS)
parse_time_test_LDADD = $(LDADD) @CHECK_LIBS@
endif

