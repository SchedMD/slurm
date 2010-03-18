#
# Makefile for sprio

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sprio

sprio_LDADD = 	$(top_builddir)/src/api/libslurm.o -ldl

noinst_HEADERS = sprio.h print.h
sprio_SOURCES = sprio.c print.c opts.c

force:
$(sprio_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sprio_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

