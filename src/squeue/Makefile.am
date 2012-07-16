#
# Makefile for squeue

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = squeue

squeue_LDADD = 	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)

noinst_HEADERS = squeue.h print.h
squeue_SOURCES = squeue.c print.c opts.c sort.c

force:
$(squeue_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

squeue_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

