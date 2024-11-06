# Makefile for task/cgroup plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(HWLOC_CPPFLAGS)

pkglib_LTLIBRARIES = task_cgroup.la

# cgroup task plugin.
task_cgroup_la_SOURCES = 	task_cgroup.h task_cgroup.c \
				task_cgroup_cpuset.h task_cgroup_cpuset.c \
				task_cgroup_memory.h task_cgroup_memory.c \
				task_cgroup_devices.h task_cgroup_devices.c
task_cgroup_la_LDFLAGS = $(PLUGIN_FLAGS)
