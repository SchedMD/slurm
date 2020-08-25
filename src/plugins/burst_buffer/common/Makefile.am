# Makefile.am for burst_buffer/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libburst_buffer_common.la
libburst_buffer_common_la_SOURCES =	\
	burst_buffer_common.c		\
	burst_buffer_common.h
