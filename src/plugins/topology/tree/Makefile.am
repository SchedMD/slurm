# Makefile for topology/tree plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = topology_tree.la

# tree topology plugin.
topology_tree_la_SOURCES = topology_tree.c \
	switch_record.c switch_record.h \
	eval_nodes_tree.c eval_nodes_tree.h

topology_tree_la_LDFLAGS = $(PLUGIN_FLAGS)
topology_tree_la_LIBADD = ../common/libtopology_common.la

force:
$(topology_tree_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
