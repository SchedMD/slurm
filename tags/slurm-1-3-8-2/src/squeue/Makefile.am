#
# Makefile for squeue

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = squeue

squeue_LDADD = 	$(top_builddir)/src/api/libslurm.o -ldl

noinst_HEADERS = squeue.h print.h
squeue_SOURCES = squeue.c print.c opts.c sort.c

squeue_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

