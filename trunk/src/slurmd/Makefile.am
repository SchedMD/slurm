# $Id$
#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd 

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS)

if WITH_ELAN
interconnect_sources = elan_interconnect.c elanhosts.c elanhosts.h
else
interconnect_sources = no_interconnect.c
endif

slurmd_LDADD = 					   \
        $(top_builddir)/src/common/libcommon.la    \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/common/libhostlist.la  \
	$(top_builddir)/src/common/libeio.la       \
	$(SSL_LIBS) 


common_sources = 	        	\
	slurmd.c slurmd.h       	\
	req.c req.h			\
	mgr.c mgr.h			\
	smgr.c smgr.h			\
	get_mach_stat.c         	\
	get_mach_stat.h			\
	read_proc.c 	        	\
	job.c job.h			\
	io.c io.h			\
	semaphore.c semaphore.h		\
	shm.c shm.h			\
	fname.c fname.h			\
	ulimits.c ulimits.h     	\
	setenvpf.c setenvpf.h   	\
	setproctitle.c setproctitle.h 	\
	interconnect.h

slurmd_SOURCES = $(common_sources) $(interconnect_sources)

slurmd_LDFLAGS = -export-dynamic

EXTRA_slurmd_SOURCES = no_interconnect.c elan_interconnect.c

force:
$(slurmd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
