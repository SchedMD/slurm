#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sbatch

sbatch_SOURCES = sbatch.c mult_cluster.c mult_cluster.h opt.c opt.h

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl

sbatch_LDADD = $(convenience_libs)

sbatch_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
