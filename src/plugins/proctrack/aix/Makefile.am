# Makefile for proctrack/aix plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -Wl,-bI:$(PROCTRACKDIR)/proctrackext.exp\
		-module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# AIX process tracking  kernel extension plugin.
pkglib_LTLIBRARIES = proctrack_aix.la
proctrack_aix_la_SOURCES = proctrack_aix.c
proctrack_aix_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_proctrack_aix_la_SOURCES = proctrack_aix.c
endif
