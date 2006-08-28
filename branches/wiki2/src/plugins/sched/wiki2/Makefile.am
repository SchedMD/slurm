# Makefile for wiki scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = sched_wiki2.la

sched_wiki2_la_SOURCES =
	crypto.c	\
	crypto.h	\
	msg.c		\
	msg.h		\
	sched_wiki.c
sched_wiki2_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
