# Makefile for squeue

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common -I$(top_srcdir)/src/api

bin_PROGRAMS = squeue

LDADD = $(top_srcdir)/src/common/libcommon.la $(top_srcdir)/src/api/libslurm.la

scontrol_SOURCES = squeue.c

