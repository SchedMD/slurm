# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
interconnect_lib = libelan_interconnect.a
else
interconnect_lib = libno_interconnect.a
endif

sbin_PROGRAMS = slurmd
noinst_LTLIBRARIES = libelan_interconnect.la \
		libno_interconnect.la \
		libforked_io.la  \
		libthreaded_io.la \
		libnbio.la \
		libcredential_utils.la

LDADD = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/common/libdaemonize.la \
	$(SSL_LIBS) libno_interconnect.la


slurmd_SOURCES = slurmd.c \
		get_mach_stat.c \
		read_proc.c \
		task_mgr.c \
		shmem_struct.c \
		circular_buffer.c \
		credential_utils.c \
		signature_utils.c \
		threaded_io.c \
		pipes.c \
		locks.c \
		reconnect_utils.c \
		io_threads.c \
		$(interconnect_lib)

libforked_io_la_SOURCES =  forked_io.c reconnect_utils.c io_threads.c 
libthreaded_io_la_SOURCES =  threaded_io.c reconnect_utils.c io_threads.c 
libnbio_la_SOURCES =  threaded_io.c nbio.c reconnect_utils.c

libelan_interconnect_la_SOURCES =  elan_interconnect.c
libno_interconnect_la_SOURCES =  no_interconnect.c 

libcredential_utils_la_SOURCES = credential_utils.c signature_utils.c

