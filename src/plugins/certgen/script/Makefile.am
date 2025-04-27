AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = certgen_script.la

certgen_script_la_SOURCES = certgen_script.c
certgen_script_la_LDFLAGS = $(PLUGIN_FLAGS)
