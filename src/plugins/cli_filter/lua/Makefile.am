# Makefile for cli_filter/lua plugin

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common \
	      $(lua_CFLAGS)

if HAVE_LUA

pkglib_LTLIBRARIES = cli_filter_lua.la

# cli_filter lua plugin.
cli_filter_lua_la_SOURCES = cli_filter_lua.c
cli_filter_lua_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
cli_filter_lua_la_LIBADD = ../common/libcli_filter_common.la \
			   $(top_builddir)/src/lua/libslurm_lua.la \
			   $(lua_LIBS)

force:
$(cli_filter_lua_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
EXTRA_cli_filter_lua_la_SOURCES = cli_filter_lua.c
endif

