#
# Makefile for sbcast

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)
bin_PROGRAMS = sbcast

depend_libs = $(top_builddir)/src/bcast/libfile_bcast.la

sbcast_LDADD = 	$(depend_libs) $(LIB_SLURM) $(DL_LIBS) -lm
sbcast_DEPENDENCIES = $(depend_libs) $(LIB_SLURM_BUILD)

noinst_HEADERS = sbcast.h
sbcast_SOURCES = sbcast.c opts.c

force:
$(sbcast_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sbcast_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

