# $Id$
#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd 

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS)

slurmd_LDADD = 					   \
        $(top_builddir)/src/common/libcommon.la    \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/common/libeio.la       \
	$(SSL_LIBS) 


slurmd_SOURCES = 	        	\
	slurmd.c slurmd.h       	\
	req.c req.h			\
	mgr.c mgr.h			\
	task.c task.h			\
	get_mach_stat.c get_mach_stat.h	\
	read_proc.c 	        	\
	slurmd_job.c slurmd_job.h			\
	io.c io.h			\
	semaphore.c semaphore.h		\
	shm.c shm.h			\
	fname.c fname.h			\
	ulimits.c ulimits.h     	\
	proctrack.c proctrack.h		\
	setproctitle.c setproctitle.h   \
	pdebug.c pdebug.h

slurmd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

force:
$(slurmd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
