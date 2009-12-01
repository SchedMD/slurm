#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmstepd

INCLUDES = -I$(top_srcdir)

slurmstepd_LDADD = 				   \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/api/libslurm.o -ldl\
	$(PLPA_LIBS) $(PAM_LIBS) $(UTIL_LIBS)

slurmstepd_SOURCES = 	        	\
	slurmstepd.c slurmstepd.h	\
	mgr.c mgr.h			\
	task.c task.h			\
	slurmstepd_job.c slurmstepd_job.h \
	io.c io.h			\
	fname.c fname.h			\
	ulimits.c ulimits.h     	\
	pdebug.c pdebug.h		\
	pam_ses.c pam_ses.h		\
	req.c req.h			\
	multi_prog.c multi_prog.h	\
	step_terminate_monitor.c step_terminate_monitor.h \
	$(top_builddir)/src/slurmd/common/proctrack.c \
	$(top_builddir)/src/slurmd/common/proctrack.h \
	$(top_builddir)/src/slurmd/common/setproctitle.c \
	$(top_builddir)/src/slurmd/common/setproctitle.h \
	$(top_builddir)/src/slurmd/common/slurmstepd_init.c \
	$(top_builddir)/src/slurmd/common/slurmstepd_init.h \
	$(top_builddir)/src/slurmd/common/run_script.c \
	$(top_builddir)/src/slurmd/common/run_script.h \
	$(top_builddir)/src/slurmd/common/task_plugin.c \
	$(top_builddir)/src/slurmd/common/task_plugin.h \
	$(top_builddir)/src/slurmd/common/set_oomadj.c \
	$(top_builddir)/src/slurmd/common/set_oomadj.h \
	$(top_builddir)/src/slurmd/common/reverse_tree.h

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
