# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd
noinst_LIBRARIES = libelan_interconnect.a libno_interconnect.a

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
		io.c \
		pipes.c

libelan_interconnect_a_SOURCES =  elan_interconnect.c
libno_interconnect_a_SOURCES =  no_interconnect.c

