# Makefile for gres/gpu plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = gres_gpu.la

# Gres GPU plugin.
gres_gpu_la_SOURCES = gres_gpu.c
gres_gpu_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
