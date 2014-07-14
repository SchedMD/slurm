#
# Makefile for sprio

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sprio

sprio_LDADD = 	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)

noinst_HEADERS = sprio.h print.h
sprio_SOURCES = sprio.c print.c opts.c

force:
$(sprio_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sprio_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

