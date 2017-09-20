# Makefile for job_submit/lua plugin

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DDEFAULT_SCRIPT_DIR=\"$(sysconfdir)\" \
	-I$(top_srcdir) -I$(top_srcdir)/src/common $(lua_CFLAGS)

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

if HAVE_LUA
 
# Job submit lua plugin.
pkglib_LTLIBRARIES = job_submit_lua.la
job_submit_lua_la_SOURCES = job_submit_lua.c
job_submit_lua_la_LDFLAGS = $(PLUGIN_FLAGS) $(lua_LIBS)

else
EXTRA_job_submit_lua_la_SOURCES = job_submit_lua.c
endif
