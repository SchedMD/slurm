# $Id$
#
# Makefile for smap

AUTOMAKE_OPTIONS = foreign

LIBS=-lncurses
INCLUDES = -I$(top_srcdir) -I 
bin_PROGRAMS = smap

smap_LDADD =					  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/api/libslurm.la

noinst_HEADERS = smap.h print.h
smap_SOURCES = smap.c opts.c print.c printjob.c sort.c sortjob.c

force:
$(smap_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

smap_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

