# Makefile for accounting_storage/ctld_relay plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = accounting_storage_ctld_relay.la

# Null job completion logging plugin.
accounting_storage_ctld_relay_la_SOURCES = accounting_storage_ctld_relay.c
accounting_storage_ctld_relay_la_LDFLAGS = $(PLUGIN_FLAGS)
accounting_storage_ctld_relay_la_LIBADD = \
	../common/libaccounting_storage_common.la

force:
$(accounting_storage_ctld_relay_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
