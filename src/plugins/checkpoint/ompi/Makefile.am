# Makefile for checkpoint/ompi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# OpenMPI checkpoint plugin.
pkglib_LTLIBRARIES = checkpoint_ompi.la
checkpoint_ompi_la_SOURCES = checkpoint_ompi.c
checkpoint_ompi_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_checkpoint_ompi_la_SOURCES = checkpoint_ompi.c
endif
