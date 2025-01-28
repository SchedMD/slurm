AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)
LDADD = -ldl -lpthread -L$(top_builddir)/src/api/.libs -lslurmfull

check_PROGRAMS = \
	$(TESTS)

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE
TESTS = topology-test

topology_test_CFLAGS = $(MYCFLAGS)
topology_test_LDADD = @CHECK_LIBS@ $(LDADD)
topology_test_LDFLAGS = -Wl,-rpath=$(top_builddir)/src/api/.libs -export-dynamic
endif
