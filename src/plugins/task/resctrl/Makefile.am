# Makefile for task/resctrl plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = task_resctrl.la
task_resctrl_la_SOURCES = resctrl_fs.c resctrl_fs.h task_resctrl.c
task_resctrl_la_LDFLAGS = $(PLUGIN_FLAGS)
