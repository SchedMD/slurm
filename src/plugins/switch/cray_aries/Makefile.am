# Makefile for switch/cray_aries plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = switch_cray_aries.la

# Cray_Aries switch plugin.
switch_cray_aries_la_SOURCES = \
	cookies.c \
	gpu.c \
	iaa.c \
	pe_info.c \
	scaling.c \
	switch_cray_aries.c \
	switch_cray_aries.h \
	util.c
switch_cray_aries_la_LDFLAGS = \
	\
	$(PLUGIN_FLAGS) \
	$(CRAY_SWITCH_LDFLAGS)
switch_cray_aries_la_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/common \
	$(CRAY_SWITCH_CPPFLAGS)
