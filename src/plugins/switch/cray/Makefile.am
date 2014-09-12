# Makefile for switch/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = switch_cray.la

# Cray switch plugin.
switch_cray_la_SOURCES = \
	cookies.c \
	gpu.c \
	iaa.c \
	pe_info.c \
	scaling.c \
	switch_cray.c \
	switch_cray.h \
	util.c
switch_cray_la_LDFLAGS = \
	$(SO_LDFLAGS) \
	$(PLUGIN_FLAGS) \
	$(CRAY_SWITCH_LDFLAGS)
switch_cray_la_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/common \
	$(CRAY_SWITCH_CPPFLAGS)
