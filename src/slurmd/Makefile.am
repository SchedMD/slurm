# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
interconnect_lib = libelan_interconnect.la
process_lib = libforked.la
io_lib = libforked_io.la
else
interconnect_lib = libno_interconnect.la
process_lib = libthreaded.la
io_lib = libthreaded_io.la
endif

#sbin_PROGRAMS = slurmd slurmd_nbio slurmd_elan
sbin_PROGRAMS = slurmd slurmd_nbio slurmd_fork_nbio

noinst_LTLIBRARIES = libelan_interconnect.la \
		libno_interconnect.la \
		libforked_io.la  \
		libthreaded_io.la \
		libnbio.la \
		libforked.la \
		libthreaded.la

LDADD = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/common/libdaemonize.la \
	$(top_srcdir)/src/common/libcred.la \
	$(SSL_LIBS)

slurmd_LDADD = $(process_lib) $(io_lib) $(LDADD) $(interconnect_lib)
slurmd_nbio_LDADD = $(process_lib) libnbio.la $(LDADD) $(interconnect_lib)
slurmd_fork_nbio_LDADD = libforked.la libnbio.la $(LDADD) $(interconnect_lib)
	
common_sources = slurmd.c \
		get_mach_stat.c \
		read_proc.c \
		task_mgr.c \
		shmem_struct.c \
		circular_buffer.c \
		pipes.c \
		locks.c \
		setenvpf.c

slurmd_SOURCES = $(common_sources)
slurmd_nbio_SOURCES = $(common_sources)
slurmd_fork_nbio_SOURCES = $(common_sources)

#slurmd_elan_LDADD = libforked.la libforked_io.la  $(LDADD) libelan_interconnect.la
#slurmd_elan_SOURCES = $(common_sources)


libforked_la_SOURCES = forked_ctrl.c
libthreaded_la_SOURCES = threaded_ctrl.c
libforked_io_la_SOURCES = forked_io.c reconnect_utils.c io_threads.c 
libthreaded_io_la_SOURCES = threaded_io.c reconnect_utils.c io_threads.c 

libnbio_la_SOURCES =  nbio.c reconnect_utils.c

libelan_interconnect_la_SOURCES =  elan_interconnect.c
libno_interconnect_la_SOURCES =  no_interconnect.c 


