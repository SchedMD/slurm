# Makefile for accounting_storage/slurmdbd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = accounting_storage_slurmdbd.la

# Null job completion logging plugin.
accounting_storage_slurmdbd_la_SOURCES = accounting_storage_slurmdbd.c \
	as_ext_dbd.c as_ext_dbd.h \
	dbd_conn.c dbd_conn.h \
	slurmdbd_agent.c slurmdbd_agent.h
accounting_storage_slurmdbd_la_LDFLAGS = $(PLUGIN_FLAGS)
accounting_storage_slurmdbd_la_LIBADD = \
	../common/libaccounting_storage_common.la

force:
$(accounting_storage_slurmdbd_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
