# $Id$
# Makefile for sacct

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sacct

sacct_LDADD =					\
	$(top_builddir)/src/common/libcommon.la	\
	$(top_builddir)/src/api/libslurm.la

sacct_SOURCES = sacct.c

force:
$(sacct_LDADD) : force
	 @cd `dirname $@` && $(MAKE) `basename $@`

sacct_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

