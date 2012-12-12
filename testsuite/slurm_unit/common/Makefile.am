AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) $(HWLOC_CPPFLAGS)
LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS) $(HWLOC_LIBS)

check_PROGRAMS = \
	$(TESTS)

TESTS = \
	pack-test \
        log-test \
	bitstring-test 

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall -ansi -pedantic -std=c99
MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
MYCFLAGS += -I$(top_srcdir)/src/common
MYCFLAGS += $(top_builddir)/src/common/libcommon.la
TESTS += xtree-test \
		 xhash-test
xtree_test_CFLAGS = $(MYCFLAGS)
xtree_test_LDADD  = @CHECK_LIBS@
xhash_test_CFLAGS = $(MYCFLAGS)
xhash_test_LDADD  = @CHECK_LIBS@
endif

