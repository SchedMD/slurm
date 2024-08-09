# Makefile for topology/block plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = topology_block.la

# Null topology plugin.
topology_block_la_SOURCES = topology_block.c \
	block_record.c block_record.h \
	eval_nodes_block.c eval_nodes_block.h
topology_block_la_LDFLAGS = $(PLUGIN_FLAGS)
topology_block_la_LIBADD = ../common/libtopology_common.la

force:
$(topology_block_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
