AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = topology_torus3d.la

# torus3d topology plugin.
topology_torus3d_la_SOURCES = topology_torus3d.c \
	torus3d_record.c torus3d_record.h \
	eval_nodes_torus3d.c eval_nodes_torus3d.h
topology_torus3d_la_LDFLAGS = $(PLUGIN_FLAGS)
topology_torus3d_la_LIBADD = ../common/libtopology_common.la


force:
$(topology_torus3d_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
