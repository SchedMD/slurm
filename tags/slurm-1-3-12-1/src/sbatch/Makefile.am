#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = sbatch

sbatch_SOURCES = sbatch.c opt.c opt.h

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl

sbatch_LDADD = $(convenience_libs) 

sbatch_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
