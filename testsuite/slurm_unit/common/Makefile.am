AUTOMAKE_OPTIONS = foreign

INCLUDES = 	-I$(top_srcdir)
LDADD =		$(top_builddir)/src/api/libslurm.o $(DL_LIBS)

check_PROGRAMS = \
	$(TESTS)

TESTS = \
	pack-test \
        log-test \
	bitstring-test 

