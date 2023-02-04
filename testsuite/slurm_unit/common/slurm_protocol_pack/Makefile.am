AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(LIB_SLURM)

check_PROGRAMS = \
	$(TESTS)

TESTS =

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@  #-Wall -ansi -pedantic -std=c99
#MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += pack_job_alloc_info_msg-test \
	 pack_priority_factors-test

pack_job_alloc_info_msg_test_CFLAGS = $(MYCFLAGS)
pack_job_alloc_info_msg_test_LDADD  = $(LDADD) @CHECK_LIBS@
pack_priority_factors_test_CFLAGS = $(MYCFLAGS)
pack_priority_factors_test_LDADD  = $(LDADD) @CHECK_LIBS@

endif
