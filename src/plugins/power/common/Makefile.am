# Makefile.am for power/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libpower_common.la
libpower_common_la_SOURCES =	\
	power_common.c		\
	power_common.h
