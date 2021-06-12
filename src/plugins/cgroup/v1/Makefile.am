# Makefile for cgroup/v1 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = cgroup_v1.la

# Cgroup v1 plugin.
cgroup_v1_la_SOURCES =	cgroup_v1.c cgroup_v1.h xcgroup.c xcgroup.h
cgroup_v1_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
cgroup_v1_la_LIBADD = ../common/libcgroup_common.la
