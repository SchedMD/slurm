# Makefile for burst_buffer/lua plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common \
	      $(lua_CFLAGS)

if HAVE_LUA

# Burst buffer lua plugin.
pkglib_LTLIBRARIES = burst_buffer_lua.la
burst_buffer_lua_la_SOURCES = burst_buffer_lua.c
burst_buffer_lua_la_LDFLAGS = $(PLUGIN_FLAGS)
burst_buffer_lua_la_LIBADD = ../common/libburst_buffer_common.la \
			     $(top_builddir)/src/lua/libslurm_lua.la \
			     $(lua_lIBS)

force:
$(burst_buffer_lua_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
