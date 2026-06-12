# Makefile for namespace/linux plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = namespace_linux.la

namespace_linux_la_SOURCES = namespace_linux.c read_nsconf.c read_nsconf.h
namespace_linux_la_LDFLAGS = $(PLUGIN_FLAGS)
