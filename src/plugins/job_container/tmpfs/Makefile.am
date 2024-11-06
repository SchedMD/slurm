# Makefile for job_container/tmpfs plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_container_tmpfs.la

# job_container/none plugin.
job_container_tmpfs_la_SOURCES = job_container_tmpfs.c read_jcconf.c \
				 read_jcconf.h
job_container_tmpfs_la_LDFLAGS = $(PLUGIN_FLAGS)
