# Makefile for select/cray plugin

AUTOMAKE_OPTIONS = foreign

CPPFLAGS = -DCRAY_CONFIG_FILE=\"$(sysconfdir)/cray.conf\"

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common -I.

pkglib_LTLIBRARIES = select_cray.la

# Cray node selection plugin.
select_cray_la_SOURCES = 	\
	select_cray.c		\
	basil_interface.h	\
	nodespec.c		\
	other_select.c 		\
	other_select.h		\
	parser_common.h		\
	cray_config.c
select_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

if HAVE_CRAY
select_cray_la_SOURCES += basil_interface.c
select_cray_la_CFLAGS   = $(MYSQL_CFLAGS)

if HAVE_CRAY_EMULATION
SUBDIRS = libemulate
select_cray_la_LIBADD   = libemulate/libalps.la
else
SUBDIRS = libalps
select_cray_la_LIBADD   = libalps/libalps.la
endif

force:
$(select_cray_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
