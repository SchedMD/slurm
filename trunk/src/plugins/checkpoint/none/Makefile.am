# $Id$
# Makefile for checkpoint/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = checkpoint_none.la

# Null checkpoint plugin.
checkpoint_none_la_SOURCES = checkpoint_none.c
checkpoint_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

if HAVE_AIX
checkpoint_none_la_LIBADD  = $(top_builddir)/src/common/libcommon.la   \
			$(top_builddir)/src/api/libslurm.la
endif
