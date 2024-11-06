# Makefile for openapi/slurmctld plugin

AUTOMAKE_OPTIONS = foreign
CLEANFILES = *.bino

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) \
	      -I$(top_srcdir)/src/slurmrestd/plugins/openapi/slurmctld/

pkglib_LTLIBRARIES = openapi_slurmctld.la

openapi_slurmctld_la_SOURCES = \
	api.c api.h assoc_mgr.c control.c diag.c jobs.c nodes.c partitions.c \
	reservations.c

openapi_slurmctld_la_LDFLAGS = $(PLUGIN_FLAGS)
