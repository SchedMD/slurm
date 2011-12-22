# Makefile for select/cons_res plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

SELECT_CONS_RES_LA_SOURCES =	\
	dist_tasks.c		\
	dist_tasks.h		\
	job_test.c job_test.h	\
	select_cons_res.c	\
	select_cons_res.h

if ! USE_LOADLEVELER

# Consumable resources node selection plugin.
pkglib_LTLIBRARIES = select_cons_res.la
select_cons_res_la_SOURCES = $(SELECT_CONS_RES_LA_SOURCES)
select_cons_res_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_select_cons_res_la_SOURCES = $(SELECT_CONS_RES_LA_SOURCES)
endif
