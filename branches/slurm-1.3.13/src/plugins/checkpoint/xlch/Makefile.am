# Makefile for checkpoint/xlch plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = checkpoint_xlch.la
checkpoint_xlch_la_SOURCES = checkpoint_xlch.c config.c
checkpoint_xlch_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

config.c: Makefile
	@( echo "char *scch_path = \"$(prefix)/sbin/scch\";"\
         ) > config.c

force:

$(checkpoint_xlch_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
