# Makefile for task/cgroup plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if HAVE_SCHED_SETAFFINITY
pkglib_LTLIBRARIES = task_cgroup.la
task_cgroup_la_SOURCES = 	\
	affinity.c affinity.h \
	task_cgroup_cpuset.c task_cgroup_cpuset.h \
	dist_tasks.c dist_tasks.h \
	schedutils.c \
	task_cgroup.c
task_cgroup_la_LDFLAGS = $(SO_LDFLAGS) $(NUMA_LIBS) $(PLPA_LIBS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_task_cgroup_la_SOURCES = \
	affinity.c affinity.h \
	task_cgroup_cpuset.c task_cgroup_cpuset.h \
	dist_tasks.c dist_tasks.h \
	schedutils.c \
	task_cgroup.c
endif
