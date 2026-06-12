# Makefile for tls/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = tls_none.la

# Null tls plugin.
tls_none_la_SOURCES = tls_none.c
tls_none_la_LDFLAGS = $(PLUGIN_FLAGS)


force:
$(tls_none_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
