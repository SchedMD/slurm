#
# Makefile for srun_cr

AUTOMAKE_OPTIONS = foreign

SRUN_CR_SOURCES = srun_cr.c

if ! USE_LOADLEVELER

CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BLCR_CPPFLAGS)

bin_PROGRAMS = srun_cr

srun_cr_SOURCES = $(SRUN_CR_SOURCES)

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl

srun_cr_LDADD = $(convenience_libs) $(BLCR_LIBS)

srun_cr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(BLCR_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_srun_cr_SOURCES = $(SRUN_CR_SOURCES)
endif
