# Makefile for select/linear plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = select_linear.la

# Linear node selection plugin.
select_linear_la_SOURCES = select_linear.c select_linear.h
select_linear_la_LDFLAGS = $(PLUGIN_FLAGS)
