# $Id$
#
# Makefile for smap

AUTOMAKE_OPTIONS = foreign

LIBS=$(NCURSES) 
INCLUDES = -I$(top_srcdir) $(BGL_INCLUDES) 
bin_PROGRAMS = smap

smap_LDADD = \
	$(top_builddir)/src/partition_allocator/libpartition_allocator.la 

noinst_HEADERS = smap.h 
smap_SOURCES = smap.c job_functions.c partition_functions.c configure_functions.c grid_functions.c opts.c

force:
$(smap_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

smap_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(BGL_LDFLAGS) 

