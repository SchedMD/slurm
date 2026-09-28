AUTOMAKE_OPTIONS = foreign subdir-objects

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/plugins/gres/shard
LDADD = -ldl -lpthread -L$(top_builddir)/src/api/.libs -lslurmfull

check_PROGRAMS = \
	$(TESTS)

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE

if CLANG
MYCFLAGS += -Xclang -pic-level -Xclang 0
endif

TESTS = gres_shard_dmem-test

gres_shard_dmem_test_SOURCES = gres_shard_dmem-test.c

gres_shard_dmem_test_CFLAGS = $(MYCFLAGS)
gres_shard_dmem_test_LDADD  = $(LDADD) @CHECK_LIBS@
# The sources are complete, so nothing here may leave a symbol unresolved:
# a loader that binds eagerly would only find out when the test is run.
gres_shard_dmem_test_LDFLAGS = -Wl,-rpath=$(top_builddir)/src/api/.libs -export-dynamic

endif
