# Makefile for swait

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = swait

swait_LDADD = $(LIB_SLURM)
swait_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS  = opt.h
swait_SOURCES = swait.c opt.c

swait_LDFLAGS = $(CMD_LDFLAGS)

REF = help.txt usage.txt
include $(top_srcdir)/make_ref.include
noinst_LTLIBRARIES = $(LIB_REF)
EXTRA_swait_DEPENDENCIES = $(LIB_REF)
swait_LDADD += $(LIB_REF)

force:
$(swait_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
