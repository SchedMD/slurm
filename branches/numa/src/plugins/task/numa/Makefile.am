# $Id: Makefile.am,v 1.1 2005/11/04 02:17:53 palermo Exp $
# Makefile for task/numa plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if HAVE_NUMA
pkglib_LTLIBRARIES = task_numa.la
task_numa_la_SOURCES = task_numa.c affinity.c schedutils.c affinity.h
task_numa_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_task_numa_la_SOURCES = task_numa.c affinity.c schedutils.c affinity.h

endif
