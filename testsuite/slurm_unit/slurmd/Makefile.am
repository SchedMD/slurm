AUTOMAKE_OPTIONS = foreign

# TESTS identifies programs that can run stand-alone at "make check" time.
TESTS =

# check_PROGRAMS identifies programs that can be built at "make check" time, 
# even if they can't be run stand-alone.
check_PROGRAMS = \
	$(TESTS) 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

LDADD   = \
	$(top_builddir)/src/common/libcommon.la \
	$(top_builddir)/src/api/libslurm.la \
	$(SSL_LIBS)

LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force :
$(LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
