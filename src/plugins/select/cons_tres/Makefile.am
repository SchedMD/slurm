# Makefile for select/cons_tres plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = select_cons_tres.la

# Trackable RESources selection plugin.
select_cons_tres_la_SOURCES = \
	cons_helpers.c cons_helpers.h \
	dist_tasks.c dist_tasks.h \
	gres_sock_list.c gres_sock_list.h \
	gres_select_filter.c gres_select_filter.h \
	gres_select_util.c gres_select_util.h \
	job_resources.c job_resources.h \
	job_test.c job_test.h \
	node_data.c node_data.h \
	part_data.c part_data.h \
	select_cons_tres.c select_cons_tres.h
select_cons_tres_la_LDFLAGS = $(PLUGIN_FLAGS)
