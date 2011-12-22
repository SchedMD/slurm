# Makefile for select/linear plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# Linear node selection plugin.
pkglib_LTLIBRARIES = select_linear.la
select_linear_la_SOURCES = select_linear.c select_linear.h
select_linear_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_select_linear_la_SOURCES = select_linear.c select_linear.h
endif
