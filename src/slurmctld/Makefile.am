# $Id$
#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS)

sbin_PROGRAMS = slurmctld

slurmctld_LDADD = 					\
	$(top_builddir)/src/common/libcommon.la    	\
	$(top_builddir)/src/common/libhostlist.la      	\
	$(top_builddir)/src/common/libdaemonize.la 


slurmctld_SOURCES = 	\
	agent.h		\
	backup.c	\
	locks.h  	\
	slurmctld.h	\
	agent.c		\
	controller.c 	\
	job_mgr.c 	\
	job_scheduler.c \
	locks.c   	\
	node_mgr.c 	\
	node_scheduler.c\
	partition_mgr.c \
	proc_req.c \
	read_config.c	\
	step_mgr.c

slurmctld_LDFLAGS = -export-dynamic

force:
$(slurmctld_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
