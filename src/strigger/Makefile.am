#
# Makefile for strigger

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)
bin_PROGRAMS = strigger

strigger_LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

noinst_HEADERS = strigger.h
strigger_SOURCES = strigger.c opts.c

force:
$(strigger_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

strigger_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

