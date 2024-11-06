# Makefile.am for jobcomp_common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libjobcomp_common.la
libjobcomp_common_la_SOURCES = jobcomp_common.c jobcomp_common.h
