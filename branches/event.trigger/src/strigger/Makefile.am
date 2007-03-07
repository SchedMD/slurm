#
# Makefile for strigger

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)
bin_PROGRAMS = strigger

strigger_LDADD =					  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/api/libslurm.la

noinst_HEADERS = strigger.h
strigger_SOURCES = sttrigger.c opts.c

force:
$(strigger_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

strigger_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

