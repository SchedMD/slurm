AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(top_builddir)/src/api/libslurm.o \
	$(top_builddir)/src/slurmd/common/libslurmd_common.o \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS) \
	$(DL_LIBS)

check_PROGRAMS = $(TESTS)



if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall -std=c99

TESTS = reverse_tree_math-test

reverse_tree_math_test_CFLAGS = $(MYCFLAGS)
reverse_tree_math_test_LDADD  = $(LDADD) @CHECK_LIBS@
endif
