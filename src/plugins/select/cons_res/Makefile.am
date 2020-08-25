# Makefile for select/cons_res plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_cons_res.la

# Consumable resources node selection plugin.
select_cons_res_la_SOURCES =  select_cons_res.c select_cons_res.h \
                              dist_tasks.c dist_tasks.h \
			      job_test.c job_test.h
select_cons_res_la_LDFLAGS = $(PLUGIN_FLAGS)
select_cons_res_la_LIBADD = ../cons_common/libcons_common.la

force:
$(select_cons_res_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
