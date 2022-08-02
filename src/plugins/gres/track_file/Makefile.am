# Makefile for gres/track_file plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = gres_track_file.la

# Gres TRACK_FILE plugin.
gres_track_file_la_SOURCES = gres_track_file.c
gres_track_file_la_LDFLAGS = $(PLUGIN_FLAGS)
gres_track_file_la_LIBADD = ../common/libgres_common.la

force:
$(gres_track_file_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
