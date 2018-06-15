#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = salloc

salloc_SOURCES = salloc.c salloc.h opt.c opt.h

convenience_libs = $(LIB_SLURM) $(DL_LIBS)
salloc_DEPENDENCIES = $(LIB_SLURM_BUILD)

salloc_LDADD = \
	$(convenience_libs)

if HAVE_ALPS_CRAY
if HAVE_REAL_CRAY
  salloc_LDADD += -ljob
endif
endif

salloc_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
