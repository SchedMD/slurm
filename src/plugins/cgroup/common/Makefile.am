# Makefile.am for cgroup/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libcgroup_common.la
libcgroup_common_la_SOURCES =	\
	cgroup_common.c
