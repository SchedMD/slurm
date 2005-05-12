# $Id$
#
# Makefile for smap
# NOTE: smap is only built and installed if HAVE_SOME_CURSES is set.
#       That means we have either curses or ncurses installed.
#

AUTOMAKE_OPTIONS = foreign

LIBS=$(NCURSES) 
INCLUDES = -I$(top_srcdir) $(BGL_INCLUDES) 

if HAVE_SOME_CURSES

bin_PROGRAMS = smap

smap_LDADD = \
	$(top_builddir)/src/partition_allocator/libpartition_allocator.la 

noinst_HEADERS = smap.h 
smap_SOURCES = smap.c \
	job_functions.c partition_functions.c \
	configure_functions.c grid_functions.c opts.c

force:
$(smap_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

smap_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(BGL_LDFLAGS) 

else

EXTRA_smap_SOURCES = smap.h smap.c \
	job_functions.c partition_functions.c \
	configure_functions.c grid_functions.c opts.c

endif


