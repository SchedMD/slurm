# Makefile.am for topology/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libtopology_common.la
libtopology_common_la_SOURCES =    \
	common_topo.c common_topo.h \
	eval_nodes.c eval_nodes.h \
	gres_filter.c gres_filter.h \
	gres_sched.c gres_sched.h
