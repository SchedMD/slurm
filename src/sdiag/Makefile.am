#
# Makefile for sdiag

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)
bin_PROGRAMS = sdiag

sdiag_LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

sdiag_SOURCES = sdiag.c opts.c

force:
$(sdiag_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sdiag_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

