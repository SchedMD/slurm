# Makefile for scancel

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/api

bin_PROGRAMS = scancel

LDADD = 					\
	$(top_builddir)/src/common/libcommon.la	\
	$(top_builddir)/src/api/libslurm.la

scancel_SOURCES = scancel.c

