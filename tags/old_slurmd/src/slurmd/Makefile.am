# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd 

if WITH_ELAN
interconnect_lib = libelan_interconnect.la
process_lib = libforked.la
io_lib = libnbio.la
noinst_LTLIBRARIES = \
	libelan_interconnect.la	\
	libforked.la		\
	libnbio.la
else
interconnect_lib = libno_interconnect.la
process_lib = libthreaded.la
io_lib = libthreaded_io.la
noinst_LTLIBRARIES = \
	libno_interconnect.la	\
	libthreaded.la		\
	libthreaded_io.la
endif

LDADD = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/common/libdaemonize.la \
	$(top_srcdir)/src/common/libcred.la \
	$(SSL_LIBS)

slurmd_LDADD = $(process_lib) $(io_lib) $(LDADD) $(interconnect_lib)
	
common_sources = slurmd.c \
		batch_mgr.c \
		get_mach_stat.c \
		read_proc.c \
		task_mgr.c \
		shmem_struct.c \
		circular_buffer.c \
		pipes.c \
		locks.c \
		setenvpf.c

slurmd_SOURCES = $(common_sources)

libforked_la_SOURCES = forked_ctrl.c
libthreaded_la_SOURCES = threaded_ctrl.c
#libforked_io_la_SOURCES = forked_io.c reconnect_utils.c io_threads.c 
libthreaded_io_la_SOURCES = threaded_io.c reconnect_utils.c io_threads.c 

libnbio_la_SOURCES =  nbio.c reconnect_utils.c

libelan_interconnect_la_SOURCES =  elan_interconnect.c
libno_interconnect_la_SOURCES =  no_interconnect.c 


