# Makefile for checkpoint/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# Null checkpoint plugin.
pkglib_LTLIBRARIES = checkpoint_none.la
checkpoint_none_la_SOURCES = checkpoint_none.c
checkpoint_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_checkpoint_none_la_SOURCES = checkpoint_none.c
endif
