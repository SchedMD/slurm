# $Id$
#
# Makefile for slurmlib

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


# hack to allow building .lo objects from other directories
../*/*.lo : %.lo : %.c
	cd `echo $@|sed 's#/[^/]*\$$##'` && $(MAKE) `echo $@|sed 's#.*/##'`
#$(filter %.lo, $(libslurm_la_LIBADD)) : %.lo : %.c
#	cd `echo $@|sed 's#/[^/]*\$$##'` && $(MAKE) `echo $@|sed 's#.*/##'`
	
#$(filter %.la, $(libslurm_la_LIBADD)) : %.la : 
#	cd `echo $@|sed 's#/[^/]*\$$##'` && $(MAKE) `echo $@|sed 's#.*/##'`
