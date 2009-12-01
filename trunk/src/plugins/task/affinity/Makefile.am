# Makefile for task/affinity plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if HAVE_SCHED_SETAFFINITY
pkglib_LTLIBRARIES = task_affinity.la
task_affinity_la_SOURCES = 	\
	affinity.c affinity.h \
	cpuset.c \
	dist_tasks.c dist_tasks.h \
	numa.c \
	schedutils.c \
	task_affinity.c
task_affinity_la_LDFLAGS = $(SO_LDFLAGS) $(NUMA_LIBS) $(PLPA_LIBS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_task_affinity_la_SOURCES = \
	affinity.c affinity.h \
	cpuset.c \
	dist_tasks.c dist_tasks.h \
	numa.c \
	schedutils.c \
	task_affinity.c
endif
