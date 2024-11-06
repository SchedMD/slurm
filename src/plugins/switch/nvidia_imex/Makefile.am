AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = switch_nvidia_imex.la

switch_nvidia_imex_la_SOURCES = \
	imex_device.c		\
	imex_device.h		\
	switch_nvidia_imex.c

switch_nvidia_imex_la_LDFLAGS = $(PLUGIN_FLAGS)
