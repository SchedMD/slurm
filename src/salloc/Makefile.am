#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

bin_PROGRAMS = salloc

salloc_SOURCES = salloc.c salloc.h opt.c opt.h

convenience_libs = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

salloc_LDADD = \
	$(convenience_libs)

if HAVE_REAL_CRAY
  salloc_LDADD += -ljob
endif

salloc_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
