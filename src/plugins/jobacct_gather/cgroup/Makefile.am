# Makefile for jobacct_gather/cgroup plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(HWLOC_CPPFLAGS)

pkglib_LTLIBRARIES = jobacct_gather_cgroup.la

jobacct_gather_cgroup_la_SOURCES = jobacct_gather_cgroup.c

jobacct_gather_cgroup_la_LDFLAGS = $(PLUGIN_FLAGS)

jobacct_gather_cgroup_la_LIBADD = ../common/libjobacct_gather_common.la

force:
$(jobacct_gather_cgroup_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
