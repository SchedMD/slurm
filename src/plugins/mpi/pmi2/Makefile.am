# Makefile for mpi/pmi2 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

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
	nameserv.c nameserv.h \
	ring.c ring.h

mpi_pmi2_la_LDFLAGS = $(PLUGIN_FLAGS)
