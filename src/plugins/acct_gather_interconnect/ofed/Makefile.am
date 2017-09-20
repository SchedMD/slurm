# Makefile for acct_gather_interconnect/ofed plugin

#dprx need to uncomment this when safe to build
#SUBDIRS = include lib

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

if BUILD_OFED

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = acct_gather_interconnect_ofed.la

# Interconnect accounting ofed plugin.
acct_gather_interconnect_ofed_la_SOURCES = acct_gather_interconnect_ofed.c \
                                     acct_gather_interconnect_ofed.h

acct_gather_interconnect_ofed_la_CFLAGS = $(OFED_CPPFLAGS)

acct_gather_interconnect_ofed_la_LDFLAGS = $(PLUGIN_FLAGS) $(OFED_LDFLAGS)
acct_gather_interconnect_ofed_la_LIBADD = $(OFED_LIBS)

else

EXTRA_acct_gather_interconnect_ofed_la_SOURCES = \
	acct_gather_interconnect_ofed.c acct_gather_interconnect_ofed.h
endif
