# Makefile for gres/mic plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = gres_mic.la

# Gres MIC plugin.
gres_mic_la_SOURCES = gres_mic.c
gres_mic_la_LDFLAGS = $(PLUGIN_FLAGS)
gres_mic_la_LIBADD = ../common/libgres_common.la

force:
$(gres_mic_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
