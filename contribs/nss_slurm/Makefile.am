#
# Makefile for nss_slurm
#

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -fPIC -I$(top_srcdir) -I$(top_srcdir)/src/common
PLUGIN_FLAGS = --export-dynamic -version-info 2:0:0

pkglib_LTLIBRARIES = libnss_slurm.la

libnss_slurm_la_SOURCES = libnss_slurm.c

libnss_slurm_la_LIBADD = $(top_builddir)/src/api/libslurm.la

libnss_slurm_la_LDFLAGS = $(PLUGIN_FLAGS) $(LIB_LDFLAGS)

force:
$(libnss_slurm_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE)
#	Don't specify basename or version.map files in src/api will not be built
#	@cd `dirname $@` && $(MAKE) `basename $@`
