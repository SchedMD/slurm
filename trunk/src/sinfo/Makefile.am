#
# Makefile for sinfo

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)
bin_PROGRAMS = sinfo

sinfo_LDADD = $(top_builddir)/src/api/libslurm.o -ldl

noinst_HEADERS = sinfo.h print.h
sinfo_SOURCES = sinfo.c opts.c print.c sort.c

force:
$(sinfo_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sinfo_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

