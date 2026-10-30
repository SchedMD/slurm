AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(lua_CFLAGS)

pkglib_LTLIBRARIES = switch_lua.la

switch_lua_la_SOURCES = switch_lua.c
switch_lua_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
switch_lua_la_LIBADD  = $(top_builddir)/src/lua/libslurm_lua.la \
			$(lua_LIBS)

force:
$(switch_lua_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
