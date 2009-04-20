#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

sbin_PROGRAMS = slurmctld

slurmctld_LDADD = 					\
	$(top_builddir)/src/common/libdaemonize.la  \
	$(top_builddir)/src/common/libcommon.o -ldl


slurmctld_SOURCES = 	\
	acct_policy.c	\
	acct_policy.h	\
	agent.c  	\
	agent.h		\
	backup.c	\
	controller.c 	\
	hilbert.c	\
	hilbert.h	\
	hilbert_slurm.c	\
	job_mgr.c 	\
	job_scheduler.c \
	job_scheduler.h \
	licenses.c	\
	licenses.h	\
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

slurmctld_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)


force:
$(slurmctld_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
