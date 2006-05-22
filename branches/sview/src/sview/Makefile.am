# $Id: Makefile.am 6669 2005-11-21 22:41:16Z da $
#
# Makefile for sview

AUTOMAKE_OPTIONS = foreign
INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

if HAVE_GTK

bin_PROGRAMS = sview

sview_LDADD =					  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/api/libslurm.la

noinst_HEADERS = sview.h
sview_SOURCES = sview.c

force:
$(sview_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sview_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) 
sview_CFLAGS = $(GTK2_CFLAGS)
#sview_LIBS = $(GTK2_LIBS)

else

EXTRA_sview_SOURCES = sview.h sview.c

endif
