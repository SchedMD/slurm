#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(SSL_CPPFLAGS)

sbin_PROGRAMS = slurmctld

slurmctld_LDADD = 					\
	$(top_builddir)/src/common/libcommon.la    	\
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
	node_scheduler.c \
	node_scheduler.h \
	partition_mgr.c \
	ping_nodes.c	\
	ping_nodes.h	\
	power_save.c	\
	proc_req.c	\
	proc_req.h	\
	read_config.c	\
	read_config.h	\
	sched_plugin.c	\
	sched_plugin.h	\
	slurmctld.h	\
	srun_comm.c	\
	srun_comm.h	\
	state_save.c	\
	state_save.h	\
	step_mgr.c	\
	trigger_mgr.c	\
	trigger_mgr.h

slurmctld_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
			$(FEDERATION_LDFLAGS)	

force:
$(slurmctld_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
