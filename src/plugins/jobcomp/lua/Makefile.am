# Makefile for jobcomp/lua plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(lua_CFLAGS)

pkglib_LTLIBRARIES = jobcomp_lua.la

# cli_filter lua plugin.
jobcomp_lua_la_SOURCES = jobcomp_lua.c
jobcomp_lua_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobcomp_lua_la_LIBADD  = $(top_builddir)/src/lua/libslurm_lua.la \
			 ../common/libjobcomp_common.la \
			 $(lua_LIBS)

force:
$(jobcomp_lua_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
