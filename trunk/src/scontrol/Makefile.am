# Makefile for scontrol

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common -I$(top_srcdir)/src/api

bin_PROGRAMS = scontrol

LDADD = $(top_srcdir)/src/common/libcommon.la $(top_srcdir)/src/api/libslurm.la

scontrol_SOURCES = scontrol.c

