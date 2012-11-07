# Makefile for acct_gather_energy/ipmi plugin

#dprx need to uncomment this when safe to build
#SUBDIRS = include lib

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = acct_gather_energy_ipmi.la

# Null energy accounting ipmi plugin.
acct_gather_energy_ipmi_la_SOURCES = acct_gather_energy_ipmi.c

#acct_gather_energy_ipmi_la_CFLAGS = $(MYSQL_CFLAGS)

acct_gather_energy_ipmi_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
