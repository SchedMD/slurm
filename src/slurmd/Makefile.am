# $Id$
#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd slurmd_step

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS) \
	-DSLURMD_STEP_PATH=\"$(prefix)/sbin/slurmd_step\"

slurmd_LDADD = 					   \
        $(top_builddir)/src/common/libcommon.la    \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/common/libeio.la       \
	$(SSL_LIBS) 


slurmd_SOURCES = 	        	\
	slurmd.c slurmd.h       	\
	req.c req.h			\
	get_mach_stat.c get_mach_stat.h	\
	read_proc.c 	        	\
	semaphore.c semaphore.h		\
	shm.c shm.h			\
	task_plugin.c task_plugin.h	\
	proctrack.c proctrack.h		\
	setproctitle.c setproctitle.h   \
	slurmd_step_init.c slurmd_step_init.h \
	run_script.c run_script.h

slurmd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

slurmd_step_LDADD = 					   \
        $(top_builddir)/src/common/libcommon.la    \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/common/libeio.la       \
	$(SSL_LIBS) 


slurmd_step_SOURCES = 	        	\
	slurmd_step.c                   \
	mgr.c mgr.h			\
	task.c task.h			\
	slurmd_job.c slurmd_job.h	\
	io.c io.h			\
	semaphore.c semaphore.h		\
	shm.c shm.h			\
	fname.c fname.h			\
	ulimits.c ulimits.h     	\
	proctrack.c proctrack.h		\
	setproctitle.c setproctitle.h   \
	task_plugin.c task_plugin.h	\
	pdebug.c pdebug.h		\
	slurmd_step_init.c slurmd_step_init.h \
	run_script.c run_script.h

slurmd_step_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

force:
$(slurmd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
