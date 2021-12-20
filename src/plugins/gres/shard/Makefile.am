# Makefile for gres/shard plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = gres_shard.la

# Gres SHARD plugin.
gres_shard_la_SOURCES = gres_shard.c
gres_shard_la_LDFLAGS = $(PLUGIN_FLAGS)
gres_shard_la_LIBADD = ../common/libgres_common.la

force:
$(gres_shard_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
