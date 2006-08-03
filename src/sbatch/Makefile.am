#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = sbatch

sbatch_SOURCES = sbatch.c opt.c opt.h

convenience_libs = \
	$(top_builddir)/src/common/libcommon.la \
	$(top_builddir)/src/api/libslurm.la

sbatch_LDADD = \
	$(convenience_libs) 

sbatch_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
