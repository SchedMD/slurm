# Makefile for acct_gather_energy/xcc plugin

#dprx need to uncomment this when safe to build
#SUBDIRS = include lib

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(FREEIPMI_CPPFLAGS)

if BUILD_IPMI

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = acct_gather_energy_xcc.la

# Energy accounting xcc plugin.
acct_gather_energy_xcc_la_SOURCES = acct_gather_energy_xcc.c

#acct_gather_energy_xcc_la_CFLAGS = $(MYSQL_CFLAGS)

acct_gather_energy_xcc_la_LDFLAGS = $(PLUGIN_FLAGS) $(FREEIPMI_LDFLAGS)
acct_gather_energy_xcc_la_LIBADD = $(FREEIPMI_LIBS)

else

EXTRA_acct_gather_energy_xcc_la_SOURCES = acct_gather_energy_xcc.c
endif
