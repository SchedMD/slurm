# Makefile for gpu/common

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# Make a .la
noinst_LTLIBRARIES = libgpu_common.la
libgpu_common_la_SOURCES =	\
	gpu_common.c
