#
# Makefile for smd

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)
bin_PROGRAMS = smd 

smd_LDADD = $(top_builddir)/src/api/libsmdns.o $(DL_LIBS) -lm

smd_SOURCES = automatic.c manual.c opt.c smd.c smd.h

force:
$(smd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

smd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS) $(READLINE_LIBS)

