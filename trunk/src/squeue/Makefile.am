# $Id$
#
# Makefile for squeue

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = squeue

LDADD = $(top_builddir)/src/common/libcommon.la 	\
	$(top_builddir)/src/api/libslurm.la

noinst_HEADERS = squeue.h print.h
squeue_SOURCES = squeue.c print.c opts.c sort.c

squeue_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

