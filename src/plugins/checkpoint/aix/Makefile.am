# Makefile for checkpoint/aix plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

# AIX checkpoint plugin.
if HAVE_AIX
pkglib_LTLIBRARIES = checkpoint_aix.la
checkpoint_aix_la_SOURCES = checkpoint_aix.c
checkpoint_aix_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
else
EXTRA_checkpoint_aix_la_SOURCES =  checkpoint_aix.c
endif
