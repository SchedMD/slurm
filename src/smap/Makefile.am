#
# Makefile for smap
# NOTE: smap is only built and installed if HAVE_SOME_CURSES is set.
#       That means we have either curses or ncurses installed.
#

AUTOMAKE_OPTIONS = foreign

LIBS=$(NCURSES)
AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)

if BUILD_SMAP

bin_PROGRAMS = smap

noinst_HEADERS = smap.h
smap_SOURCES = smap.c \
	job_functions.c partition_functions.c \
	grid_functions.c reservation_functions.c opts.c

smap_LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

if BLUEGENE_LOADED
bg_dir = $(top_builddir)/src/plugins/select/bluegene

smap_LDADD += $(bg_dir)/libconfigure_api.la

smap_SOURCES += configure_functions.c

endif

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


