#
# Makefile for smap
# NOTE: smap is only built and installed if HAVE_SOME_CURSES is set.
#       That means we have either curses or ncurses installed.
#

AUTOMAKE_OPTIONS = foreign

LIBS=$(NCURSES)
INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

SMAP_SOURCES =			\
	grid_functions.c	\
	job_functions.c		\
	opts.c			\
	partition_functions.c	\
	reservation_functions.c	\
	smap.c smap.h


if BUILD_SMAP

bin_PROGRAMS = smap

smap_SOURCES = $(SMAP_SOURCES)

smap_LDADD = $(top_builddir)/src/api/libslurm.o -ldl

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
EXTRA_smap_SOURCES = $(SMAP_SOURCES)
endif


