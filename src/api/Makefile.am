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
# Update META file information before a public release only!
# 
# Note that libtool builds libslurm.so.<major>.<age>.<rev>
# where major = API_MAJOR = (API_CURRENT - API_AGE)
# and   age   = API_AGE
# and   rev   = API_REVISION
#
# The following examples are based upon initial values of API_CURRENT = 6,
# API_AGE = 4, API_REVISION = 6.
#
# If any interfaces have been added: increment API_CURRENT, increment API_AGE,
# and set API_REVISION = 0 (e.g. libslurm.so.2.4.6 -> libslurm.so.2.5.0)
#
# If any interfaces have been removed: increment API_CURRENT, set API_AGE = 0, 
# and set API_REVISION = 0 (e.g. libslurm.so.2.4.6 -> libslurm.so.7.0.0)
#
# If any interfaces have changed: increment API_CURRENT only
# (e.g. libslurm.so.2.4.6 -> libslurm.so.3.4.6)
#
# If the interfaces have not changed, but underlying code has changed at all: 
# increment API_REVISION only (e.g. libslurm.so.2.4.6 -> libslurm.so.2.4.7)
#
current = $(SLURM_API_CURRENT)
age     = $(SLURM_API_AGE)
rev     = $(SLURM_API_REVISION)

lib_LTLIBRARIES = libslurm.la

libslurm_la_SOURCES =    \
	allocate.c       \
	bnr.c            \
	cancel.c         \
	checkpoint.c     \
	complete.c       \
	config_info.c    \
	init_msg.c       \
	job_info.c job_info.h \
	job_step_info.c  \
	node_info.c      \
	node_select_info.c node_select_info.h \
	partition_info.c \
	spawn.c          \
	submit.c         \
	reconfigure.c    \
	update_config.c  

BUILT_SOURCES = 	\
	$(VERSION_SCRIPT)

common_dir = $(top_builddir)/src/common

libslurm_la_LIBADD         = $(common_dir)/libcommon.la -lpthread

libslurm_la_LDFLAGS        = \
	$(LIB_LDFLAGS)       \
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
