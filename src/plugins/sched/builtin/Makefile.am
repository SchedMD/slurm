# Makefile for builtin scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

SCHED_BUILTIN_LA_SOURCES =	\
	builtin_wrapper.c	\
	builtin.c		\
	builtin.h

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = sched_builtin.la
sched_builtin_la_SOURCES = $(SCHED_BUILTIN_LA_SOURCES)
sched_builtin_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_sched_builtin_la_SOURCES = $(SCHED_BUILTIN_LA_SOURCES)
endif
