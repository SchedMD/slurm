#
# Makefile for slurmsted

AUTOMAKE_OPTIONS = foreign

SLURMSTEPD_SOURCES = 	        	\
	fname.c fname.h			\
	io.c io.h			\
	mgr.c mgr.h			\
	multi_prog.c multi_prog.h	\
	pam_ses.c pam_ses.h		\
	pdebug.c pdebug.h		\
	req.c req.h			\
	slurmstepd.c slurmstepd.h	\
	slurmstepd_job.c		\
	slurmstepd_job.h		\
	step_terminate_monitor.c	\
	step_terminate_monitor.h	\
	task.c task.h			\
	ulimits.c ulimits.h

if ! USE_LOADLEVELER

sbin_PROGRAMS = slurmstepd

INCLUDES = -I$(top_srcdir)

slurmstepd_LDADD = 				   \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/api/libslurm.o -ldl \
	$(PLPA_LIBS) $(PAM_LIBS) $(UTIL_LIBS) \
	../common/libslurmd_common.la

slurmstepd_SOURCES = $(SLURMSTEPD_SOURCES)
if HAVE_AIX
# We need to set maxdata back to 0 because this effects the "max memory size"
# ulimit, and the ulimit is inherited by child processes.
slurmstepd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) -Wl,-bmaxdata:0x0
else
slurmstepd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)
endif

force:
$(slurmd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_slurmstepd_SOURCES = $(SLURMSTEPD_SOURCES)
endif
