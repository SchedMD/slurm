# Makefile for sview

AUTOMAKE_OPTIONS = foreign
AM_CPPFLAGS = -I$(top_srcdir)

if BUILD_SVIEW

bin_PROGRAMS = sview

sview_LDADD = $(LIB_SLURM)
sview_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = sview.h gthread_helper.h
sview_SOURCES = sview.c popups.c grid.c part_info.c job_info.c \
	front_end_info.c node_info.c resv_info.c \
	submit_info.c admin_info.c common.c \
	config_info.c defaults.c gthread_helper.c bb_info.c

force:
$(sview_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sview_LDFLAGS = $(CMD_LDFLAGS) $(BG_LDFLAGS) $(GTK_LIBS)
sview_CFLAGS = $(GTK_CFLAGS) -DGTK_DISABLE_DEPRECATED

else

EXTRA_sview_SOURCES = sview.h sview.c popups.c grid.c part_info.c job_info.c \
	front_end_info.c node_info.c resv_info.c gthread_helper.c \
	submit_info.c admin_info.c common.c config_info.c defaults.c

endif
