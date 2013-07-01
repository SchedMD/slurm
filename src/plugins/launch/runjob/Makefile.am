# Makefile for launch/runjob plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(BG_INCLUDES)

pkglib_LTLIBRARIES = launch_runjob.la

launch_runjob_la_SOURCES = launch_runjob.c

if BGQ_LOADED
launch_runjob_la_SOURCES += runjob_interface.cc
# because totalview needs the libraries on load we link in srun and not here.
#launch_runjob_la_LDFLAGS += $(RUNJOB_LDFLAGS)
endif

launch_runjob_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
