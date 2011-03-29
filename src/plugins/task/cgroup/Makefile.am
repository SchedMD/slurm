# Makefile for task/cgroup plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = task_cgroup.la

# cgroup task plugin.
task_cgroup_la_SOURCES = 	task_cgroup.c \
				task_cgroup_cpuset.h task_cgroup_cpuset.c \
				task_cgroup_memory.h task_cgroup_memory.c
task_cgroup_la_CPPFLAGS = $(HWLOC_CPPFLAGS)
task_cgroup_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(HWLOC_LDFLAGS) $(HWLOC_LIBS)
