# $Id$
#
# Makefile for slurm API library

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

# libslurm version information : 
#
# Update this information before a public release only!
# 
# If library source code has changed at all increment rev
# (current:rev:age) = (current:rev+1:age)
#
# If any interfaces have been added,removed, or changed increment current
#  and set rev = 0;
#
# If any interfaces have been added increment age
#
# If any interfaces have been removed, set age = 0
#
current = 1
rev     = 0
age     = 0

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

common_dir = $(top_builddir)/src/common

libslurm_la_LIBADD         = \
	$(common_dir)/libcommon.la -lpthread

libslurm_la_LDFLAGS        = \
	-export-symbols libslurm.sym           \
	-version-info $(current):$(rev):$(age) 

libslurm_la_DEPENDENCIES   = \
	libslurm.sym \
	$(common_dir)/libcommon.la

libslurm.sym : $(top_builddir)/slurm/slurm.h
	-sed -n 's/^extern .* \([a-zA-Z0-9_]*\) PARAMS.*$$/\1/p' $< $* >libslurm.sym

distclean-local: 
	-rm -rf libslurm.map libslurm.sym

force:
$(libslurm_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
