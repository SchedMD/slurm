#
# Makefile for smap
# NOTE: smap is only built and installed if HAVE_SOME_CURSES is set.
#       That means we have either curses or ncurses installed.
#

AUTOMAKE_OPTIONS = foreign

LIBS=$(NCURSES) 
INCLUDES = -I$(top_srcdir) $(BG_INCLUDES) 

if HAVE_SOME_CURSES

bin_PROGRAMS = smap

smap_LDADD = \
	$(top_builddir)/src/plugins/select/bluegene/block_allocator/libbluegene_block_allocator.la \
	$(top_builddir)/src/api/libslurm.o -ldl

noinst_HEADERS = smap.h 
smap_SOURCES = smap.c \
	job_functions.c partition_functions.c \
	configure_functions.c grid_functions.c \
	reservation_functions.c opts.c

force:
$(smap_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

smap_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

else

EXTRA_smap_SOURCES = smap.h smap.c \
	job_functions.c partition_functions.c \
	configure_functions.c grid_functions.c \
	reservation_functions.c opts.c

endif


