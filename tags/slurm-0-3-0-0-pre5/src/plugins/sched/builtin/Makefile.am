# $Id$
# Makefile for builtin scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = sched_builtin.la

sched_builtin_la_SOURCES = builtin_wrapper.c

sched_builtin_la_LDFLAGS = $(PLUGIN_FLAGS)
sched_builtin_la_LIBADD = $(top_builddir)/src/api/libslurm.la
