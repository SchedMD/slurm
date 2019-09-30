# Makefile for gpu/rsmi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(RSMI_CPPFLAGS)

RSMI_SOURCES = gpu_rsmi.c

pkglib_LTLIBRARIES = gpu_rsmi.la
gpu_rsmi_la_SOURCES = $(RSMI_SOURCES)
# gpu_rsmi_la_LDFLAGS = $(PLUGIN_FLAGS) $(RSMI_LDFLAGS)
gpu_rsmi_la_LDFLAGS = $(PLUGIN_FLAGS)
# gpu_rsmi_la_LIBADD = $(RSMI_LIBS)
