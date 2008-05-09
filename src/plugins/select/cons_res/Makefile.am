# Makefile for select/cons_res plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_cons_res.la

# Consumable resources node selection plugin.
select_cons_res_la_SOURCES =  select_cons_res.c select_cons_res.h \
                              dist_tasks.c dist_tasks.h
select_cons_res_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
