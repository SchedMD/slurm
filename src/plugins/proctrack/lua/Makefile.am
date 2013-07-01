# Makefile for proctrack/lua plugin

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DDEFAULT_SCRIPT_DIR=\"$(sysconfdir)\" \
	-I$(top_srcdir) -I$(top_srcdir)/src/common $(lua_CFLAGS)

PLUGIN_FLAGS = - -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES       = proctrack_lua.la
proctrack_lua_la_SOURCES = proctrack_lua.c
proctrack_lua_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(lua_LIBS)
