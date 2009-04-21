# Makefile for checkpoint/blcr plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = checkpoint_blcr.la
checkpoint_blcr_la_SOURCES = checkpoint_blcr.c
checkpoint_blcr_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

dist_bin_SCRIPTS = cr_checkpoint.sh cr_restart.sh


force:

$(checkpoint_blcr_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
