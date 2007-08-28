# Makefile for jobacct-gather/aix plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct-gather_aix.la

# Null job completion logging plugin.
jobacct-gather_aix_la_SOURCES = jobacct-gather_aix.c \
	$(top_builddir)/src/slurmd/common/proctrack.c \
	$(top_builddir)/src/slurmd/common/proctrack.h 

jobacct-gather_aix_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct-gather_aix_la_LIBADD  =  \
	$(top_builddir)/src/common/libcommon.la -lpthread 
