# Makefile for scancel

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/api $(POPT_INCLUDES)

bin_PROGRAMS = scancel

LDADD = 					\
	$(top_builddir)/src/common/libcommon.la	\
	$(top_builddir)/src/api/libslurm.la	\
	$(POPT_LIBS) 

noinst_HEADERS  = scancel.h
scancel_SOURCES = scancel.c opt.c

