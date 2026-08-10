# Makefile for relay_io/uds_json

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = relay_io_uds_json.la

# uds_json relay_io plugin.
relay_io_uds_json_la_SOURCES = relay_io_uds_json.c
relay_io_uds_json_la_LDFLAGS = $(PLUGIN_FLAGS)


force:
$(relay_io_uds_json_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
