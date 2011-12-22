#
# Makefile for sview

AUTOMAKE_OPTIONS = foreign
INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

SVIEW_SOURCES =			\
	admin_info.c		\
	block_info.c		\
	common.c		\
	config_info.c		\
	defaults.c		\
	front_end_info.c	\
	grid.c			\
	job_info.c		\
	node_info.c		\
	part_info.c		\
	popups.c		\
	resv_info.c		\
	submit_info.c		\
	sview.c sview.h

if BUILD_SVIEW

bin_PROGRAMS = sview

sview_LDADD = $(top_builddir)/src/db_api/libslurmdb.o -ldl

sview_SOURCES = $(SVIEW_SOURCES)

force:
$(sview_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sview_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(BG_LDFLAGS) $(GTK_LIBS)
sview_CFLAGS = $(GTK_CFLAGS)

else
EXTRA_sview_SOURCES = $(SVIEW_SOURCES)
endif
