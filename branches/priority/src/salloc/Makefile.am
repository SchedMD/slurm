#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

bin_PROGRAMS = salloc

salloc_SOURCES = salloc.c salloc.h opt.c opt.h

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl -lm

salloc_LDADD = \
	$(convenience_libs) 

salloc_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
