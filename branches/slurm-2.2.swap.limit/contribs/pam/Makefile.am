#
# Makefile for SLURM PAM library
#

AUTOMAKE_OPTIONS = foreign

CPPFLAGS = -fPIC
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common
PLUGIN_FLAGS = -module --export-dynamic -avoid-version

pkglibdir = $(libdir)/security

if HAVE_PAM
pam_lib = pam_slurm.la
else
pam_lib =
endif

pkglib_LTLIBRARIES = $(pam_lib)

if HAVE_PAM

current = $(SLURM_API_CURRENT)
age     = $(SLURM_API_AGE)
rev     = $(SLURM_API_REVISION)

pam_slurm_la_SOURCES = 				\
	pam_slurm.c				\
	$(top_builddir)/src/common/hostlist.c	\
	$(top_builddir)/src/common/hostlist.h

pam_slurm_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(LIB_LDFLAGS)

else
EXTRA_pam_slurm_la_SOURCES = pam_slurm.c
endif

