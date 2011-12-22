# Makefile for priority/multifactor plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

PRIORITY_MULTIFACTOR_LA_SOURCES = priority_multifactor.c

if ! USE_LOADLEVELER

# Null priority logging plugin.
pkglib_LTLIBRARIES = priority_multifactor.la
priority_multifactor_la_SOURCES = $(PRIORITY_MULTIFACTOR_LA_SOURCES)
priority_multifactor_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
priority_multifactor_la_LIBADD  = -lm

else
pkglib_LTLIBRARIES =
EXTRA_priority_multifactor_la_SOURCES = $(PRIORITY_MULTIFACTOR_LA_SOURCES)
endif
