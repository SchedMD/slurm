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
	basil_interface.c \
	basil_interface.h \
	controller.c 	\
	gang.c		\
	gang.h		\
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
	port_mgr.c	\
	port_mgr.h	\
	power_save.c	\
	preempt.c	\
	preempt.h	\
	proc_req.c	\
	proc_req.h	\
	read_config.c	\
	read_config.h	\
	reservation.c	\
	reservation.h	\
	sched_plugin.c	\
	sched_plugin.h	\
	slurmctld.h	\
	srun_comm.c	\
	srun_comm.h	\
	state_save.c	\
	state_save.h	\
	step_mgr.c	\
	topo_plugin.c	\
	topo_plugin.h	\
	trigger_mgr.c	\
	trigger_mgr.h

slurmctld_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)


force:
$(slurmctld_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
