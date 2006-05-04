# $Id: Makefile.am,v 1.1 2005/11/04 02:17:53 palermo Exp $
# Makefile for task/affinity plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if HAVE_SCHED_SETAFFINITY
pkglib_LTLIBRARIES = task_affinity.la
task_affinity_la_SOURCES = task_affinity.c affinity.c schedutils.c affinity.h
task_affinity_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_task_affinity_la_SOURCES = task_affinity.c affinity.c schedutils.c affinity.h

endif
