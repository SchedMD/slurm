#
# Makefile for strigger

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)
bin_PROGRAMS = strigger

strigger_LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

noinst_HEADERS = strigger.h
strigger_SOURCES = strigger.c opts.c

force:
$(strigger_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

strigger_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

