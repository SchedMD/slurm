# Makefile.am for common_as

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libaccounting_storage_common.la
libaccounting_storage_common_la_SOURCES =    \
	common_as.c common_as.h
