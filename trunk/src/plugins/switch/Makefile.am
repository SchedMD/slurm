# $Id$
# Makefile for switch plugins

AUTOMAKE_OPTIONS = foreign

if HAVE_ELAN
elan_lib = switch_elan.la
else
elan_lib =
endif

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = switch_none.la $(elan_lib)

# Null switch plugin.
switch_none_la_SOURCES = switch_none.c
switch_none_la_LDFLAGS = $(PLUGIN_FLAGS)

if HAVE_ELAN
# Quadrics elan3 or elan4 switch plugin.
switch_elan_la_SOURCES =        \
	switch_elan.c		\
	elanhosts.c elanhosts.h	\
	qsw.c qsw.h
switch_elan_la_LIBADD  = $(top_builddir)/src/common/libcommon.la $(ELAN_LIBS)
switch_elan_la_LDFLAGS = $(PLUGIN_FLAGS)
endif
