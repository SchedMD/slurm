# Makefile for mpi/lam plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_lam.la

mpi_lam_la_SOURCES = mpi_lam.c lam.h
mpi_lam_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
