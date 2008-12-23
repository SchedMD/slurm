# Makefile for select/3d_torus plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_3d_torus.la

# 3-D torus node selection plugin.
select_3d_torus_la_SOURCES = select_3d_torus.c select_3d_torus.h
select_3d_torus_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

