# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
interconnect_lib = libelan_interconnect.la
procces_lib = libforked_io.la
else
interconnect_lib = libno_interconnect.la
procces_lib = libthreaded_io.la
endif

sbin_PROGRAMS = slurmd
noinst_LTLIBRARIES = libelan_interconnect.la \
		libno_interconnect.la \
		libforked_io.la  \
		libthreaded_io.la \
		libnbio.la

LDADD = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/common/libdaemonize.la \
	$(top_srcdir)/src/common/libcred.la \
	$(interconnect_lib) \
	$(procces_lib) \
	$(SSL_LIBS)


slurmd_SOURCES = slurmd.c \
		get_mach_stat.c \
		read_proc.c \
		task_mgr.c \
		shmem_struct.c \
		circular_buffer.c \
		pipes.c \
		locks.c

libforked_io_la_SOURCES = forked_io.c reconnect_utils.c io_threads.c 
libthreaded_io_la_SOURCES = threaded_io.c reconnect_utils.c io_threads.c 

libnbio_la_SOURCES =  threaded_io.c nbio.c reconnect_utils.c

libelan_interconnect_la_SOURCES =  elan_interconnect.c
libno_interconnect_la_SOURCES =  no_interconnect.c 


