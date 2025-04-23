AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = topology_flat.la

topology_flat_la_SOURCES = topology_flat.c
topology_flat_la_LDFLAGS = $(PLUGIN_FLAGS)
topology_flat_la_LIBADD = ../common/libtopology_common.la

force:
$(topology_flat_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
