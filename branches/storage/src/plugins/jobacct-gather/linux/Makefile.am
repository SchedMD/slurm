# Makefile for jobacct-gather/linux plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct-gather_linux.la

# Null job completion logging plugin.
jobacct-gather_linux_la_SOURCES = jobacct-gather_linux.c \
	$(top_builddir)/src/slurmd/common/proctrack.c \
	$(top_builddir)/src/slurmd/common/proctrack.h 

jobacct-gather_linux_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct-gather_linux_la_LIBADD  =  \
	$(top_builddir)/src/common/libcommon.la -lpthread 
