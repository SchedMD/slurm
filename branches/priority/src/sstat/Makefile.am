# Makefile for sstat

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sstat

sstat_LDADD = $(top_builddir)/src/api/libslurm.o -ldl


noinst_HEADERS = sstat.c 
sstat_SOURCES = sstat.c process.c print.c options.c

force:
$(sstat_LDADD) : force
	 @cd `dirname $@` && $(MAKE) `basename $@`

sstat_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

