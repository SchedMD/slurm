# Makefile for select/linear plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_linear.la

# Linear node selection plugin.
select_linear_la_SOURCES = select_linear.c
select_linear_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
select_linear_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

