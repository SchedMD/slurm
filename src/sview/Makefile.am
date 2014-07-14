# $Id: Makefile.am 6669 2005-11-21 22:41:16Z da $
#
# Makefile for sview

AUTOMAKE_OPTIONS = foreign
AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)

if BUILD_SVIEW

bin_PROGRAMS = sview

sview_LDADD = $(top_builddir)/src/db_api/libslurmdb.o $(DL_LIBS)

noinst_HEADERS = sview.h
sview_SOURCES = sview.c popups.c grid.c part_info.c job_info.c \
	block_info.c front_end_info.c node_info.c resv_info.c \
	submit_info.c admin_info.c common.c \
	config_info.c defaults.c gthread_helper.c

force:
$(sview_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sview_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(BG_LDFLAGS) $(GTK_LIBS)
sview_CFLAGS = $(GTK_CFLAGS)

else

EXTRA_sview_SOURCES = sview.h sview.c popups.c grid.c part_info.c job_info.c \
	block_info.c front_end_info.c node_info.c resv_info.c gthread_helper.c \
	submit_info.c admin_info.c common.c config_info.c defaults.c

endif
