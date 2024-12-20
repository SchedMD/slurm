AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread

check_PROGRAMS = \
	$(TESTS)

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
MYCFLAGS += -Wl,-rpath=$(top_builddir)/src/api/.libs -L$(top_builddir)/src/api/.libs -lslurmfull -export-dynamic
TESTS = topology-test

topology_test_CFLAGS = $(MYCFLAGS)
topology_test_LDADD = @CHECK_LIBS@
endif
