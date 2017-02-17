AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

check_PROGRAMS = \
	$(TESTS)

TESTS =

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@  #-Wall -ansi -pedantic -std=c99
#MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += pack_user_rec-test \
	 pack_cluster_rec-test \
	 pack_used_limits-test \
	 pack_account_rec-test \
	 pack_coord_rec-test

pack_user_rec_test_CFLAGS = $(MYCFLAGS)
pack_user_rec_test_LDADD  = $(LDADD) @CHECK_LIBS@

pack_cluster_rec_test_CFLAGS = $(MYCFLAGS)
pack_cluster_rec_test_LDADD  = $(LDADD) @CHECK_LIBS@

pack_used_limits_test_CFLAGS = $(MYCFLAGS)
pack_used_limits_test_LDADD  = $(LDADD) @CHECK_LIBS@

pack_account_rec_test_CFLAGS = $(MYCFLAGS)
pack_account_rec_test_LDADD  = $(LDADD) @CHECK_LIBS@

pack_coord_rec_test_CFLAGS = $(MYCFLAGS)
pack_coord_rec_test_LDADD  = $(LDADD) @CHECK_LIBS@

endif
