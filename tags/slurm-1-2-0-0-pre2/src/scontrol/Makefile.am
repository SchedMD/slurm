# Makefile for scontrol

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = scontrol

scontrol_SOURCES = scontrol.c

convenience_libs = $(top_builddir)/src/api/libslurmhelper.la

scontrol_LDADD = \
	$(convenience_libs) \
	$(READLINE_LIBS)

scontrol_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
