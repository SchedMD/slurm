# Makefile for squeue

AUTOMAKE_OPTIONS = foreign

INCLUDES = $(POPT_INCLUDES) -I$(top_srcdir) -I$(top_srcdir)/src/api
bin_PROGRAMS = sinfo

LDADD = $(top_builddir)/src/common/libcommon.la \
	$(top_builddir)/src/api/libslurm.la \
	$(POPT_LIBS)

noinst_HEADERS = sinfo.h
sinfo_SOURCES = sinfo.c opts.c 
#noinst_HEADERS = sinfo.h print.h
#squeue_SOURCES = sinfo.c print.c opts.c 

