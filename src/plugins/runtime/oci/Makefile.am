# Makefile for runtime/oci plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = runtime_oci.la

# OCI runtime plugin.
runtime_oci_la_SOURCES = runtime_oci.c
runtime_oci_la_LDFLAGS = $(PLUGIN_FLAGS)


force:
$(runtime_oci_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
