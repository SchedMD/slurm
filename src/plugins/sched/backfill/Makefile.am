# Makefile for backfill scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

SCHED_BACKFILL_LA_SOURCES =	\
	backfill_wrapper.c	\
	backfill.c		\
	backfill.h

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = sched_backfill.la
sched_backfill_la_SOURCES = $(SCHED_BACKFILL_LA_SOURCES)
sched_backfill_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_sched_backfill_la_SOURCES = $(SCHED_BACKFILL_LA_SOURCES)
endif
