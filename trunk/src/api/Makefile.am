# $Id$
#
# Makefile for slurm API library

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

if WITH_GNU_LD
VERSION_SCRIPT = \
	version.map
OTHER_FLAGS = \
	-Wl,--version-script=$(VERSION_SCRIPT)
endif

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
current = $(SLURM_API_CURRENT)
rev     = $(SLURM_API_REVISION)
age     = $(SLURM_API_AGE)

lib_LTLIBRARIES = libslurm.la

libslurm_la_SOURCES =    \
	allocate.c       \
	bnr.c            \
	cancel.c         \
	complete.c       \
	config_info.c    \
	init_msg.c       \
	job_info.c       \
	job_info.h       \
	job_step_info.c  \
	node_info.c      \
	partition_info.c \
	submit.c         \
	reconfigure.c    \
	update_config.c  

BUILT_SOURCES = 	\
	$(VERSION_SCRIPT)

common_dir = $(top_builddir)/src/common

libslurm_la_LIBADD         = $(common_dir)/libcommon.la -lpthread

libslurm_la_LDFLAGS        = \
	-version-info $(current):$(rev):$(age) \
	$(OTHER_FLAGS)

libslurm_la_DEPENDENCIES   = $(common_dir)/libcommon.la

force:
$(libslurm_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

$(VERSION_SCRIPT) :
	(echo "{ global:";   \
	 echo "   BNR_*;";   \
	 echo "   slurm_*;"; \
	 echo "  local: *;"; \
	 echo "};") > $(VERSION_SCRIPT)
	
DISTCLEANFILES = \
	$(VERSION_SCRIPT)
