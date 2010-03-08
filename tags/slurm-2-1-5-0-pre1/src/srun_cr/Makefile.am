#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BLCR_CPPFLAGS)

bin_PROGRAMS = srun_cr

srun_cr_SOURCES = srun_cr.c

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl

srun_cr_LDADD = $(convenience_libs) $(BLCR_LIBS)

srun_cr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(BLCR_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

