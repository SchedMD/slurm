# Makefile.am for relay_io_common

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = librelay_io_common.la
librelay_io_common_la_SOURCES =    \
	relay_io_common.c relay_io_common.h \
	ring_buffer.c ring_buffer.h
