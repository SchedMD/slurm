# $Id$
# Makefile for scancel

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) $(POPT_INCLUDES)

bin_PROGRAMS = scancel

scancel_LDADD = 					\
	$(top_builddir)/src/common/libcommon.la	\
	$(top_builddir)/src/api/libslurm.la	\
	$(POPT_LIBS) 

noinst_HEADERS  = scancel.h
scancel_SOURCES = scancel.c opt.c

force:
$(scancel_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
