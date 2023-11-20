#
# Makefile for sinfo

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)
bin_PROGRAMS = sinfo

sinfo_LDADD = $(LIB_SLURM)
sinfo_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = sinfo.h print.h
sinfo_SOURCES = sinfo.c opts.c print.c sort.c

REF = help.txt usage.txt
include $(top_srcdir)/make_ref.include
noinst_LTLIBRARIES = $(LIB_REF)
EXTRA_sinfo_DEPENDENCIES = $(LIB_REF)
sinfo_LDADD += $(LIB_REF)

force:
$(sinfo_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sinfo_LDFLAGS = $(CMD_LDFLAGS)
