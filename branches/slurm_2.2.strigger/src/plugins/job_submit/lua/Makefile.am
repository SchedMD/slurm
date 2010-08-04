# Makefile for job_submit/lua plugin

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DDEFAULT_SCRIPT_DIR=\"$(sysconfdir)\"

PLUGIN_FLAGS = -module -avoid-version --export-dynamic
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(lua_CFLAGS)

pkglib_LTLIBRARIES = job_submit_lua.la

# Job submit lua plugin.
job_submit_lua_la_SOURCES = job_submit_lua.c
job_submit_lua_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(lua_LIBS)
