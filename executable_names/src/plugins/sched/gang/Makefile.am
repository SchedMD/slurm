# Makefile for gang scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = sched_gang.la

sched_gang_la_SOURCES = \
	gang.c		\
	gang.h		\
	sched_gang.c
sched_gang_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
