AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = topology_ring.la

# ring topology plugin.
topology_ring_la_SOURCES = topology_ring.c \
	ring_record.c ring_record.h \
	eval_nodes_ring.c eval_nodes_ring.h
topology_ring_la_LDFLAGS = $(PLUGIN_FLAGS)
topology_ring_la_LIBADD = ../common/libtopology_common.la


force:
$(topology_ring_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
