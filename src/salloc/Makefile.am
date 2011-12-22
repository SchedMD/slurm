#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

bin_PROGRAMS = salloc

salloc_SOURCES =		\
	load_leveler.c		\
	load_leveler.h		\
	opt.c opt.h		\
	salloc.c salloc.h

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl

salloc_LDADD = \
	$(convenience_libs)

if HAVE_REAL_CRAY
  salloc_LDADD += -ljob
endif
if USE_LOADLEVELER
  salloc_LDADD += $(UTIL_LIBS)
endif

salloc_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
