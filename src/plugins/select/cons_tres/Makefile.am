# Makefile for select/cons_tres plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_cons_tres.la

# Trackable RESources selection plugin.
select_cons_tres_la_SOURCES =	dist_tasks.c dist_tasks.h	\
				job_test.c job_test.h		\
				select_cons_tres.c select_cons_tres.h
select_cons_tres_la_LDFLAGS = $(PLUGIN_FLAGS)
