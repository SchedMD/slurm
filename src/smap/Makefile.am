#
# Makefile for smap
# NOTE: smap is only built and installed if HAVE_SOME_CURSES is set.
#       That means we have either curses or ncurses installed.
#

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

if BUILD_SMAP

bin_PROGRAMS = smap

noinst_HEADERS = smap.h
smap_SOURCES = smap.c \
	job_functions.c partition_functions.c \
	grid_functions.c reservation_functions.c opts.c

smap_LDADD = $(LIB_SLURM) $(DL_LIBS) $(NCURSES)

smap_DEPENDENCIES = $(LIB_SLURM_BUILD)

force:
$(smap_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

smap_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

else

EXTRA_smap_SOURCES = smap.h smap.c \
	job_functions.c partition_functions.c \
	grid_functions.c \
	reservation_functions.c opts.c

endif
