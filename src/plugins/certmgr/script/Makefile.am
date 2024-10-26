# Makefile for certmgr/script plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = certmgr_script.la

certmgr_script_la_SOURCES = certmgr_script.c
certmgr_script_la_LDFLAGS = $(PLUGIN_FLAGS)

force:
$(certmgr_script_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
