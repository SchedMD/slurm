AUTOMAKE_OPTIONS = foreign subdir-objects

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread -I$(top_srcdir)/src/plugins/sched/backfill

check_PROGRAMS = \
	$(TESTS)


if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition

MYCFLAGS += -Wl,-rpath=$(top_builddir)/src/api/.libs -L$(top_builddir)/src/api/.libs -lslurmfull -export-dynamic

TESTS = backfill-test

backfill_test_SOURCES = backfill_test.c	\
			dummy_functions.c \
			$(top_srcdir)/src/plugins/sched/backfill/backfill.c	\
			$(top_srcdir)/src/plugins/sched/backfill/backfill.h	\
			$(top_srcdir)/src/plugins/sched/backfill/oracle.c	\
			$(top_srcdir)/src/plugins/sched/backfill/oracle.h	\
			$(top_srcdir)/src/slurmctld/job_scheduler.c		\
			$(top_srcdir)/src/slurmctld/node_scheduler.c

backfill_test_CFLAGS = $(MYCFLAGS)
backfill_test_LDADD  = $(LDADD) @CHECK_LIBS@
backfill_test_LDFLAGS = -z lazy

endif
