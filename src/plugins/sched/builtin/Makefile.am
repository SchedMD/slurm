# Makefile for builtin scheduler plugin

AUTOMAKE_OPTIONS = foreign

AM_CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = sched_builtin.la

sched_builtin_la_SOURCES = \
			builtin_wrapper.c \
			builtin.c	\
			builtin.h
sched_builtin_la_LDFLAGS = $(PLUGIN_FLAGS)
