# $Id$
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd

LDADD = $(top_srcdir)/src/common/libcommon.la \
	$(SSL_LIBS)


slurmd_SOURCES = slurmd.c \
		get_mach_stat.c \
		read_proc.c \
		task_mgr.c \
		shmem_struct.c \
		circular_buffer.c \
		credential_utils.c \
		io.c \
		pipes.c

