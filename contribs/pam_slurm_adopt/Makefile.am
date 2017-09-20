#
# Makefile for pam_slurm_adopt
#

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -fPIC -I$(top_srcdir) -I$(top_srcdir)/src/common
# -DLIBSLURM_SO=\"$(libdir)/libslurm.so\"
PLUGIN_FLAGS = -module --export-dynamic -avoid-version

pkglibdir = $(PAM_DIR)

if HAVE_PAM
pam_lib = pam_slurm_adopt.la
else
pam_lib =
endif

pkglib_LTLIBRARIES = $(pam_lib)

if HAVE_PAM

current = $(SLURM_API_CURRENT)
age     = $(SLURM_API_AGE)
rev     = $(SLURM_API_REVISION)

pam_slurm_adopt_la_SOURCES = pam_slurm_adopt.c helper.c helper.h

pam_slurm_adopt_la_LIBADD = $(top_builddir)/src/api/libslurm.la

pam_slurm_adopt_la_LDFLAGS = $(PLUGIN_FLAGS) $(LIB_LDFLAGS)

force:
$(pam_slurm_adopt_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE)
#	Don't specify basename or version.map files in src/api will not be built
#	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_pam_slurm_adopt_la_SOURCES = pam_slurm_adopt.c helper.c
endif

