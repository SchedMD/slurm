# $Id$
#
# Makefile for slurm API library

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = slurm.h libslurm.sym

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

lib_LTLIBRARIES = libslurm.la

libslurm_la_SOURCES =    \
	allocate.c       \
	cancel.c         \
	complete.c       \
	config_info.c    \
	free_msg.c       \
	init_msg.c       \
	job_info.c       \
	job_step_info.c  \
	node_info.c      \
	partition_info.c \
	submit.c         \
	reconfigure.c    \
	update_config.c   

include_HEADERS =	 \
	slurm.h

common_dir = $(top_builddir)/src/common

libslurm_la_LIBADD        = $(common_dir)/libcommon.la
libslurm_la_LDFLAGS       = -export-symbols $(srcdir)/libslurm.sym
libslurm_la_DEPENDENCIES  = $(srcdir)/libslurm.sym $(libslurm_la_LIBADD)

force:
$(libslurm_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
