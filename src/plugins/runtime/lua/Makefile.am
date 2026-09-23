# Makefile for runtime/lua plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(lua_CFLAGS)

pkglib_LTLIBRARIES = runtime_lua.la

# Lua runtime plugin.
runtime_lua_la_SOURCES = runtime_lua.c
runtime_lua_la_LDFLAGS = $(PLUGIN_FLAGS)
runtime_lua_la_LIBADD = $(top_builddir)/src/lua/libslurm_lua.la \
			$(lua_LIBS)


force:
$(runtime_lua_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
