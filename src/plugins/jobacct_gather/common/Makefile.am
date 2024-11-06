# Makefile.am for jobacct_gather/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libjobacct_gather_common.la
libjobacct_gather_common_la_SOURCES =    \
	common_jag.c common_jag.h
