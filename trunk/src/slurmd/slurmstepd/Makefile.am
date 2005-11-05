# $Id$
#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmstepd

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS)

slurmstepd_LDADD = 				   \
        $(top_builddir)/src/common/libcommon.la    \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/common/libeio.la       \
	$(SSL_LIBS) 


slurmstepd_SOURCES = 	        	\
	slurmstepd.c			\
	mgr.c mgr.h			\
	task.c task.h			\
	slurmstepd_job.c slurmstepd_job.h \
	io.c io.h			\
	fname.c fname.h			\
	ulimits.c ulimits.h     	\
	pdebug.c pdebug.h		\
	req.c req.h			\
	$(top_builddir)/src/slurmd/common/proctrack.c \
	$(top_builddir)/src/slurmd/common/proctrack.h \
	$(top_builddir)/src/slurmd/common/setproctitle.c \
	$(top_builddir)/src/slurmd/common/setproctitle.h \
	$(top_builddir)/src/slurmd/common/slurmstepd_init.c \
	$(top_builddir)/src/slurmd/common/slurmstepd_init.h \
	$(top_builddir)/src/slurmd/common/stepd_api.h \
	$(top_builddir)/src/slurmd/common/run_script.c \
	$(top_builddir)/src/slurmd/common/run_script.h \
	$(top_builddir)/src/slurmd/common/task_plugin.c \
	$(top_builddir)/src/slurmd/common/task_plugin.h

slurmstepd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

force:
$(slurmd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
