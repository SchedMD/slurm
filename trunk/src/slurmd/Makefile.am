# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd 

INCLUDES = $(SSL_CPPFLAGS)

if WITH_ELAN
interconnect_lib = libelan_interconnect.la
noinst_LTLIBRARIES = 		\
	libelan_interconnect.la	
else
interconnect_lib = libno_interconnect.la
noinst_LTLIBRARIES = 		\
	libno_interconnect.la	
endif

LDADD = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/common/libdaemonize.la \
	$(top_srcdir)/src/common/libcred.la \
	$(SSL_LIBS)

slurmd_LDADD = $(LDADD) $(interconnect_lib)

	
common_sources = 	        \
	slurmd.c slurmd.h       \
	req.c req.h		\
	mgr.c mgr.h		\
	get_mach_stat.c         \
	get_mach_stat.h		\
	read_proc.c 	        \
	read_proc.h 	        \
	job.c job.h		\
	io.c io.h		\
	semaphore.c semaphore.h	\
	shm.c shm.h		\
	setenvpf.c setenvpf.h

slurmd_SOURCES = $(common_sources)

libelan_interconnect_la_SOURCES = elan_interconnect.c
libno_interconnect_la_SOURCES   = no_interconnect.c

