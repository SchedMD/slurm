#
# Makefile for sinfo

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)
bin_PROGRAMS = sinfo

sinfo_LDADD = $(LIB_SLURM) $(DL_LIBS)
sinfo_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = sinfo.h print.h
sinfo_SOURCES = sinfo.c opts.c print.c sort.c

force:
$(sinfo_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sinfo_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

