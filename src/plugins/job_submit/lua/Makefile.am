# Makefile for job_submit/lua plugin

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common \
	      $(lua_CFLAGS)

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

if HAVE_LUA

# Job submit lua plugin.
pkglib_LTLIBRARIES = job_submit_lua.la
job_submit_lua_la_SOURCES = job_submit_lua.c
job_submit_lua_la_LDFLAGS = $(PLUGIN_FLAGS)
job_submit_lua_la_LIBADD  = $(top_builddir)/src/lua/libslurm_lua.la \
			    $(lua_LIBS)

force:
$(job_submit_lua_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
EXTRA_job_submit_lua_la_SOURCES = job_submit_lua.c
endif
