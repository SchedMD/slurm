# Makefile for select/alps plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common -I. \
	-I$(top_srcdir)/src/plugins/select/other

pkglib_LTLIBRARIES = select_alps.la

# Alps node selection plugin.
select_alps_la_SOURCES = 	\
	select_alps.c		\
	basil_interface.h	\
	nodespec.c		\
	parser_common.h		\
	cray_config.c		\
	cray_config.h
select_alps_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

select_alps_la_LIBADD   = ../other/libother_select.la

if HAVE_ALPS_CRAY
select_alps_la_SOURCES += basil_interface.c
select_alps_la_CFLAGS   = $(MYSQL_CFLAGS)

if HAVE_ALPS_CRAY_EMULATION
SUBDIRS = libemulate
select_alps_la_LIBADD  += libemulate/libalps.la
else
SUBDIRS = libalps
select_alps_la_LIBADD  += libalps/libalps.la
endif

endif

force:
$(select_alps_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

