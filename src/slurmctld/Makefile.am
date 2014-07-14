#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)

# noinst_LTLIBRARIES = libslurmctld.la
# libslurmctld_la_LDFLAGS  = $(LIB_LDFLAGS) -module --export-dynamic
# libslurmctld_la_SOURCES =
slurmctld_SOURCES =     \
	acct_policy.c	\
	acct_policy.h	\
	agent.c  	\
	agent.h		\
	backup.c	\
	controller.c 	\
	front_end.c	\
	front_end.h	\
	gang.c		\
	gang.h		\
	groups.c	\
	groups.h	\
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
	slurmctld_plugstack.c \
	slurmctld_plugstack.h \
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
	statistics.c	\
	step_mgr.c	\
	trigger_mgr.c	\
	trigger_mgr.h


sbin_PROGRAMS = slurmctld

slurmctld_LDADD = 				    \
	$(top_builddir)/src/common/libdaemonize.la  \
	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)
slurmctld_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(slurmctld_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
