# Makefile for common/interfaces

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

# making a .la

noinst_PROGRAMS = \
	libcommon_interfaces.o \
	libslurmctld_interfaces.o \
	libslurmd_interfaces.o

noinst_LTLIBRARIES = \
	libcommon_interfaces.la \
	libslurmctld_interfaces.la \
	libslurmd_interfaces.la

libcommon_interfaces_la_SOURCES =    \
	accounting_storage.c			\
	accounting_storage.h			\
	acct_gather.c				\
	acct_gather.h				\
	acct_gather_energy.c			\
	acct_gather_energy.h			\
	acct_gather_filesystem.c		\
	acct_gather_filesystem.h		\
	acct_gather_interconnect.c		\
	acct_gather_interconnect.h		\
	acct_gather_profile.c			\
	acct_gather_profile.h			\
	auth.c					\
	auth.h					\
	cgroup.c				\
	cgroup.h				\
	cli_filter.c				\
	cli_filter.h				\
	cred.h					\
	cred.c					\
	data_parser.c				\
	data_parser.h				\
	ext_sensors.c				\
	ext_sensors.h				\
	gpu.c					\
	gpu.h					\
	gres.c					\
	gres.h					\
	hash.c					\
	hash.h					\
	jobacct_gather.c			\
	jobacct_gather.h			\
	jobcomp.c				\
	jobcomp.h				\
	mcs.c					\
	mcs.h					\
	mpi.c					\
	mpi.h					\
	node_features.c				\
	node_features.h				\
	openapi.c				\
	openapi.h				\
	power.c					\
	power.h					\
	prep.c					\
	prep.h					\
	priority.c				\
	priority.h				\
	route.c					\
	route.h					\
	select.c				\
	select.h				\
	serializer.c				\
	serializer.h				\
	site_factor.c				\
	site_factor.h				\
	switch.c				\
	switch.h				\
	topology.c				\
	topology.h

libslurmctld_interfaces_la_SOURCES =    \
	burst_buffer.c	\
	burst_buffer.h	\
	job_submit.c	\
	job_submit.h	\
	preempt.c	\
	preempt.h	\
	sched_plugin.c	\
	sched_plugin.h

libslurmd_interfaces_la_SOURCES =    \
	core_spec.c \
	core_spec.h \
	job_container.c \
	job_container.h \
	proctrack.c \
	proctrack.h \
	task.c \
	task.h

# This was made so we could export all symbols from libcommon_interfaces
# on multiple platforms
libcommon_interfaces_o_SOURCES =
libcommon_interfaces.o$(EXEEXT) :  $(libcommon_interfaces_la_OBJECTS) $(libcommon_interfaces_la_DEPENDENCIES)
	$(LINK)  $(libcommon_interfaces_la_OBJECTS)

# This was made so we could export all symbols from libslurmctld_interfaces
# on multiple platforms
libslurmctld_interfaces_o_SOURCES =
libslurmctld_interfaces.o$(EXEEXT) :  $(libslurmctld_interfaces_la_OBJECTS) $(libslurmctld_interfaces_la_DEPENDENCIES)
	$(LINK)  $(libslurmctld_interfaces_la_OBJECTS)

# This was made so we could export all symbols from libslurmd_interfaces
# on multiple platforms
libslurmd_interfaces_o_SOURCES =
libslurmd_interfaces.o$(EXEEXT) :  $(libslurmd_interfaces_la_OBJECTS) $(libslurmd_interfaces_la_DEPENDENCIES)
	$(LINK)  $(libslurmd_interfaces_la_OBJECTS)
