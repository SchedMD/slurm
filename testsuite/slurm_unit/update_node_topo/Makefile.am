AUTOMAKE_OPTIONS = foreign subdir-objects

AM_CPPFLAGS = -I$(top_srcdir)
LDADD = -ldl -lpthread -L$(top_builddir)/src/api/.libs -lslurmfull

check_PROGRAMS = \
	$(TESTS)

EXTRA_DIST = slurm.conf topology.yaml

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE

if CLANG
MYCFLAGS += -Xclang -pic-level -Xclang 0
endif

TESTS = update_node_topo-test

update_node_topo_test_SOURCES = update_node_topo-test.c \
				$(top_srcdir)/src/slurmctld/node_mgr.c

update_node_topo_test_CFLAGS = $(MYCFLAGS)
update_node_topo_test_LDADD = $(LDADD) @CHECK_LIBS@
update_node_topo_test_LDFLAGS = -z lazy -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition -Wl,-rpath=$(top_builddir)/src/api/.libs -export-dynamic
endif
