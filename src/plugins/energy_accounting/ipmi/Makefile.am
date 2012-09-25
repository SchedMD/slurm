# Makefile for energy_accounting/ipmi plugin

#dprx need to uncomment this when safe to build
#SUBDIRS = include lib
	
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = energy_accounting_ipmi.la

# Null energy accounting ipmi plugin.
energy_accounting_ipmi_la_SOURCES = energy_accounting_ipmi.c 
       
#energy_accounting_ipmi_la_CFLAGS = $(MYSQL_CFLAGS)

energy_accounting_ipmi_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

