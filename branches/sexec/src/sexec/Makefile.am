# $Id: Makefile.am 6669 2005-11-21 22:41:16Z da $
#
# Makefile for sexec

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)
bin_PROGRAMS = sexec

sexec_LDADD =					  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/api/libslurm.la

sexec_SOURCES = sexec.c

force:
$(sexec_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sexec_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

