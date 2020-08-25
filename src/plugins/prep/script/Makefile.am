# Makefile for prep/script plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = prep_script.la

# PrEP script plugin.
prep_script_la_SOURCES =		\
	prep_script.c			\
	prep_script.h			\
	prep_script_slurmctld.c		\
	prep_script_slurmd.c

prep_script_la_LDFLAGS = $(PLUGIN_FLAGS)
