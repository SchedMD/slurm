# $Id$
#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS)

sbin_PROGRAMS = slurmctld

slurmctld_LDADD = 					\
	$(top_builddir)/src/common/libcommon.la    	\
	$(top_builddir)/src/common/libcred.la      	\
	$(top_builddir)/src/common/libdaemonize.la 


slurmctld_SOURCES = 	\
	agent.h		\
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
	read_config.c	\
	step_mgr.c

force:
$(slurmctld_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
