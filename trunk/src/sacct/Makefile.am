# Makefile for sacct

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sacct

sacct_LDADD = 	$(top_builddir)/src/api/libslurm.o -ldl

sacct_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

noinst_HEADERS = sacct.c 
sacct_SOURCES = sacct.c process.c print.c options.c sacct_stat.c

force:
$(sacct_LDADD) : force
	 @cd `dirname $@` && $(MAKE) `basename $@`


