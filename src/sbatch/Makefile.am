#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sbatch

sbatch_SOURCES = sbatch.c opt.c opt.h xlate.c

convenience_libs = $(LIB_SLURM) $(DL_LIBS)
sbatch_DEPENDENCIES = $(LIB_SLURM_BUILD)

sbatch_LDADD = $(convenience_libs)

sbatch_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
