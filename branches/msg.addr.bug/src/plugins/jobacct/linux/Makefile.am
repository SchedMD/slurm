# Makefile for jobacct/linux plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_linux.la

# Null job completion logging plugin.
jobacct_linux_la_SOURCES = jobacct_linux.c \
	$(top_builddir)/src/plugins/jobacct/common/jobacct_common.c \
	$(top_builddir)/src/plugins/jobacct/common/common_slurmctld.c \
	$(top_builddir)/src/plugins/jobacct/common/common_slurmstepd.c \
	$(top_builddir)/src/slurmd/common/proctrack.c \
	$(top_builddir)/src/slurmd/common/proctrack.h 

jobacct_linux_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
