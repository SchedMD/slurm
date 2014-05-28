# Makefile for mpi/pmi2 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_pmi2.la

mpi_pmi2_la_SOURCES = mpi_pmi2.c \
	agent.c agent.h \
	client.c client.h \
	kvs.c kvs.h \
	info.c info.h \
	pmi1.c pmi2.c pmi.h \
	setup.c setup.h \
	spawn.c spawn.h \
	tree.c tree.h \
	nameserv.c nameserv.h

mpi_pmi2_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

mpi_pmi2_la_LIBADD = \
	$(top_builddir)/src/slurmd/common/libslurmd_reverse_tree_math.la

force:

$(mpi_pmi2_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
