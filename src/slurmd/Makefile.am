# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
interconnect_lib = libelan_interconnect.a
else
interconnect_lib = libno_interconnect.a
endif

sbin_PROGRAMS = slurmd
noinst_LIBRARIES = libelan_interconnect.a libno_interconnect.a libforked_io.a  libthreaded_io.a libnbio.a

LDADD = $(top_srcdir)/src/common/libcommon.la \
	$(SSL_LIBS) libno_interconnect.a


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

libforked_io_a_SOURCES =  forked_io.c reconnect_utils.c io_threads.c 
libthreaded_io_a_SOURCES =  threaded_io.c reconnect_utils.c io_threads.c 
libnbio_a_SOURCES =  threaded_io.c nbio.c reconnect_utils.c

libelan_interconnect_a_SOURCES =  elan_interconnect.c
libno_interconnect_a_SOURCES =  no_interconnect.c 

