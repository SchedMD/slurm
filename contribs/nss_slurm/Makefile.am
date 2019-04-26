#
# Makefile for nss_slurm
#

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -fPIC -I$(top_srcdir) -I$(top_srcdir)/src/common
# -DLIBSLURM_SO=\"$(libdir)/libslurm.so\"
PLUGIN_FLAGS = -module --export-dynamic -avoid-version

nss_lib = nss_slurm.la

pkglib_LTLIBRARIES = $(nss_lib)

current = $(SLURM_API_CURRENT)
age     = $(SLURM_API_AGE)
rev     = $(SLURM_API_REVISION)

nss_slurm_la_SOURCES = nss_slurm.c

nss_slurm_la_LIBADD = $(top_builddir)/src/api/libslurm.la

nss_slurm_la_LDFLAGS = $(PLUGIN_FLAGS) $(LIB_LDFLAGS)

force:
$(nss_slurm_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE)
#	Don't specify basename or version.map files in src/api will not be built
#	@cd `dirname $@` && $(MAKE) `basename $@`
