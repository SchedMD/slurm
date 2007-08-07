# Makefile for jobcomp/database plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_database.la

# Database job completion logging plugin.
jobcomp_database_la_SOURCES = jobcomp_database.c
jobcomp_database_la_LIBADD  =  \
	$(top_builddir)/src/common/libcommon.la -lpthread

jobcomp_database_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobcomp_database_la_DEPENDENCIES  = \
	$(top_builddir)/src/common/libcommon.la
