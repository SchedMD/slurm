# $Id$
# Makefile for backfill scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = sched_backfill.la

sched_backfill_la_SOURCES = backfill_wrapper.c	\
			backfill.c	\
			backfill.h
sched_backfill_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

if HAVE_AIX
sched_backfill_la_LIBADD = $(top_builddir)/src/common/libcommon.la
endif
