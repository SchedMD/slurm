#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

# noinst_LTLIBRARIES = libslurmctld.la
# libslurmctld_la_LDFLAGS  = $(LIB_LDFLAGS) -module --export-dynamic
# libslurmctld_la_SOURCES =
slurmctld_SOURCES =     \
	acct_policy.c	\
	acct_policy.h	\
	agent.c  	\
	agent.h		\
	backup.c	\
	burst_buffer.c	\
	burst_buffer.h	\
	controller.c 	\
	crontab.c 	\
	fed_mgr.c 	\
	fed_mgr.h 	\
	front_end.c	\
	front_end.h	\
	gang.c		\
	gang.h		\
	gres_ctld.c	\
	gres_ctld.h	\
	groups.c	\
	groups.h	\
	heartbeat.c	\
	heartbeat.h	\
	job_mgr.c 	\
	job_scheduler.c	\
	job_scheduler.h	\
	job_submit.c	\
	job_submit.h	\
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
	power_save.h	\
	preempt.c	\
	preempt.h	\
	prep_slurmctld.c \
	proc_req.c	\
	proc_req.h	\
	read_config.c	\
	read_config.h	\
	reservation.c	\
	reservation.h	\
	rpc_queue.c	\
	rpc_queue.h	\
	sched_plugin.c	\
	sched_plugin.h	\
	slurmctld.h	\
	slurmctld_plugstack.c \
	slurmctld_plugstack.h \
	slurmscriptd.c \
	slurmscriptd.h \
	slurmscriptd_protocol_defs.c \
	slurmscriptd_protocol_defs.h \
	slurmscriptd_protocol_pack.c \
	slurmscriptd_protocol_pack.h \
	srun_comm.c	\
	srun_comm.h	\
	state_save.c	\
	state_save.h	\
	statistics.c	\
	step_mgr.c	\
	trigger_mgr.c	\
	trigger_mgr.h


sbin_PROGRAMS = slurmctld

slurmctld_LDADD = $(LIB_SLURM) $(DL_LIBS)
slurmctld_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

slurmctld_DEPENDENCIES = $(LIB_SLURM_BUILD)

force:
$(slurmctld_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
