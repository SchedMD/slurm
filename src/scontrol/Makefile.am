# $Id$
# Makefile for scontrol

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = scontrol

LDADD = 					  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/api/libslurm.la       \
	$(READLINE_LIBS)

scontrol_SOURCES = scontrol.c

scontrol_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

