AUTOMAKE_OPTIONS = foreign subdir-objects

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/plugins/sched/backfill
LDADD = -ldl -lpthread -L$(top_builddir)/src/api/.libs -lslurmfull

check_PROGRAMS = \
	$(TESTS)


if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE

if CLANG
MYCFLAGS += -Xclang -pic-level -Xclang 0
endif

TESTS = backfill-test

backfill_test_SOURCES = backfill_test.c	\
			dummy_functions.c \
			$(top_srcdir)/src/plugins/sched/backfill/backfill.c	\
			$(top_srcdir)/src/plugins/sched/backfill/backfill.h	\
			$(top_srcdir)/src/plugins/sched/backfill/oracle.c	\
			$(top_srcdir)/src/plugins/sched/backfill/oracle.h	\
			$(top_srcdir)/src/slurmctld/job_scheduler.c		\
			$(top_srcdir)/src/slurmctld/licenses.c			\
			$(top_srcdir)/src/slurmctld/node_scheduler.c

backfill_test_CFLAGS = $(MYCFLAGS)
backfill_test_LDADD  = $(LDADD) @CHECK_LIBS@
backfill_test_LDFLAGS = -z lazy  -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition  -Wl,-rpath=$(top_builddir)/src/api/.libs -export-dynamic

endif
