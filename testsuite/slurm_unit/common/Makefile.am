AUTOMAKE_OPTIONS = foreign

SUBDIRS = bitstring \
	  hostlist \
	  slurm_protocol_defs \
	  slurm_protocol_pack \
	  slurmdb_defs \
	  slurmdb_pack

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(LIB_SLURM)

check_PROGRAMS = \
	$(TESTS)

TESTS = \
	log-test

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += xhash-test \
	 data-test \
	 slurm_opt-test \
	 xstring-test \
	 parse_time-test \
	 job-resources-test \
	 pack-test \
	 reverse_tree-test

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
job_resources_test_CFLAGS = $(MYCFLAGS)
job_resources_test_LDADD = $(LDADD) @CHECK_LIBS@
pack_test_CFLAGS = $(MYCFLAGS)
pack_test_LDADD = $(LDADD) @CHECK_LIBS@
reverse_tree_test_CFLAGS = $(MYCFLAGS)
reverse_tree_test_LDADD = $(LDADD) @CHECK_LIBS@
endif

