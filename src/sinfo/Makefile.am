# Makefile for squeue

AUTOMAKE_OPTIONS = foreign

INCLUDES = $(POPT_INCLUDES) -I$(top_srcdir)/src/common -I$(top_srcdir)/src/api

bin_PROGRAMS = sinfo

LDADD = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/api/libslurm.la \
	$(POPT_LIBS)

noinst_HEADERS = sinfo.h
sinfo_SOURCES = sinfo.c opts.c 
#noinst_HEADERS = sinfo.h print.h
#squeue_SOURCES = sinfo.c print.c opts.c 

