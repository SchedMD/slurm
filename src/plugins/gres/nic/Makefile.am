# Makefile for gres/nic plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = gres_nic.la

# Gres NIC plugin.
gres_nic_la_SOURCES = gres_nic.c
gres_nic_la_LDFLAGS = $(PLUGIN_FLAGS)
gres_nic_la_LIBADD = ../common/libgres_common.la

force:
$(gres_nic_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
