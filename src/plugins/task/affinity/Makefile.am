# Makefile for task/affinity plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = task_affinity.la
task_affinity_la_SOURCES = 	\
	affinity.c affinity.h \
	dist_tasks.c dist_tasks.h \
	numa.c \
	schedutils.c \
	task_affinity.c
task_affinity_la_LDFLAGS = $(NUMA_LIBS) $(PLUGIN_FLAGS)
