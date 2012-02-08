# Makefile.am for libpermapi for poe to interface with

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

# making a .la

pkglib_LTLIBRARIES = libpermapi.la

libpermapi_la_SOURCES = shr_64.c
libpermapi_la_LDFLAGS = -export-dynamic $(SO_LDFLAGS) $(PLUGIN_FLAGS) \
	$(top_builddir)/src/api/libslurm.o -ldl -lpthread
