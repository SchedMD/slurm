# Makefile for select/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common -I.

pkglib_LTLIBRARIES = select_cray.la

# Cray node selection plugin.
select_cray_la_SOURCES = select_cray.c basil_interface.h	\
			 other_select.c other_select.h
select_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

if HAVE_NATIVE_CRAY
SUBDIRS = libalps

select_cray_la_SOURCES += basil_interface.c
select_cray_la_CFLAGS   = $(MYSQL_CFLAGS)
select_cray_la_LIBADD   = libalps/libalps.la

force:
$(select_cray_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
SUBDIRS = libemulate

select_cray_la_SOURCES += basil_interface.c
select_cray_la_CFLAGS   = $(MYSQL_CFLAGS)
select_cray_la_LIBADD   = libemuate/libalps.la

force:
$(select_cray_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
