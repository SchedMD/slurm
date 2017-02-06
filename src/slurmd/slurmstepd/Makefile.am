#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmstepd

AM_CPPFLAGS = -I$(top_srcdir)

depend_libs = 				   \
	$(top_builddir)/src/common/libdaemonize.la \
	../common/libslurmd_common.o

slurmstepd_LDADD = $(depend_libs) $(LIB_SLURM) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS) \
	$(DL_LIBS) $(PAM_LIBS) $(UTIL_LIBS)

slurmstepd_SOURCES = 	        	\
	slurmstepd.c slurmstepd.h	\
	mgr.c mgr.h			\
	task.c task.h			\
	slurmstepd_job.c slurmstepd_job.h \
	io.c io.h			\
	ulimits.c ulimits.h     	\
	pdebug.c pdebug.h		\
	pam_ses.c pam_ses.h		\
	req.c req.h			\
	multi_prog.c multi_prog.h	\
	step_terminate_monitor.c step_terminate_monitor.h

slurmstepd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

slurmstepd_DEPENDENCIES = $(depend_libs) $(LIB_SLURM_BUILD)

force:
$(slurmstepd_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
