# Makefile for jobacct_gather/linux plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_gather_linux.la

# Null job completion logging plugin.
jobacct_gather_linux_la_SOURCES = jobacct_gather_linux.c \
	$(top_builddir)/src/slurmd/common/proctrack.c \
	$(top_builddir)/src/slurmd/common/proctrack.h 

jobacct_gather_linux_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

