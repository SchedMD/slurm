# Makefile for gres/mic plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(HWLOC_CPPFLAGS)

pkglib_LTLIBRARIES = gres_mic.la

# Gres MIC plugin.
gres_mic_la_SOURCES = gres_mic.c
gres_mic_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(HWLOC_LDFLAGS)
gres_mic_la_LIBADD  = $(HWLOC_LIBS)
