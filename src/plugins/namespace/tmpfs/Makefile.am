# Makefile for namespace/tmpfs plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = namespace_tmpfs.la

namespace_tmpfs_la_SOURCES = namespace_tmpfs.c read_jcconf.c \
				 read_jcconf.h
namespace_tmpfs_la_LDFLAGS = $(PLUGIN_FLAGS)
