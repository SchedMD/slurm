# Makefile for scancel

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common -I$(top_srcdir)/src/api

bin_PROGRAMS = scancel

LDADD = $(top_srcdir)/src/common/libcommon.la $(top_srcdir)/src/api/libslurm.la

scancel_SOURCES = scancel.c

