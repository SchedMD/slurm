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
	agent.c  	\
	agent.h		\
	backup.c	\
	controller.c 	\
	job_mgr.c 	\
	job_scheduler.c \
	locks.c   	\
	locks.h  	\
	node_mgr.c 	\
	node_scheduler.c\
	partition_mgr.c \
	ping_nodes.c	\
	ping_nodes.h	\
	proc_req.c	\
	read_config.c	\
	slurmctld.h	\
	step_mgr.c

slurmctld_LDFLAGS = -export-dynamic

force:
$(slurmctld_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
