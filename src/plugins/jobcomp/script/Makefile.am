# $Id$
# Makefile for jobcomp/script plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_script.la

# Script job completion logging plugin.
jobcomp_script_la_SOURCES = jobcomp_script.c
jobcomp_script_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

if HAVE_AIX
jobcomp_script_la_LIBADD  = $(top_builddir)/src/common/libcommon.la   \
			$(top_builddir)/src/api/libslurm.la
endif
