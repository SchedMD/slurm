# Makefile.am for gres_common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libgres_common.la
libgres_common_la_SOURCES =    \
	gres_common.c gres_common.h gres_c_s.c gres_c_s.h
