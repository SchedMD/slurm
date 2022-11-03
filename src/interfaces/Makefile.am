# Makefile for common/interfaces

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

# making a .la

noinst_PROGRAMS = libcommon_interfaces.o
noinst_LTLIBRARIES = \
	libcommon_interfaces.la

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
	site_factor.c				\
	site_factor.h				\
	topology.c				\
	topology.h

# This was made so we could export all symbols from libcommon_interfaces
# on multiple platforms
libcommon_interfaces_o_SOURCES =
libcommon_interfaces.o$(EXEEXT) :  $(libcommon_interfaces_la_OBJECTS) $(libcommon_interfaces_la_DEPENDENCIES)
	$(LINK)  $(libcommon_interfaces_la_OBJECTS)
