# Makefile for hres/lua plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) \
	      $(lua_CFLAGS)

# HRES lua plugin.
pkglib_LTLIBRARIES = hres_lua.la
hres_lua_la_SOURCES = hres_lua.c
hres_lua_la_LDFLAGS = $(PLUGIN_FLAGS)
hres_lua_la_LIBADD = $(top_builddir)/src/lua/libslurm_lua.la \
		     $(lua_LIBS)

force:
$(hres_lua_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
