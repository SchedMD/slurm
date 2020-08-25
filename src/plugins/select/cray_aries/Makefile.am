# Makefile for select/cray_aries plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/plugins/select/other $(CRAY_SELECT_CPPFLAGS)

pkglib_LTLIBRARIES = select_cray_aries.la

# Cray/Aries node selection plugin.
select_cray_aries_la_SOURCES = ccm.c ccm.h select_cray_aries.c
select_cray_aries_la_LDFLAGS = $(PLUGIN_FLAGS) $(CRAY_SELECT_LDFLAGS)
select_cray_aries_la_LIBADD  = ../other/libother_select.la
