#
# Makefile for SLURM PAM library
#

AUTOMAKE_OPTIONS = foreign

CFLAGS = -fPIC
INCLUDES = -I$(top_srcdir)/src -I$(top_srcdir)/..
LIBS =
PLUGIN_FLAGS = -lgcc -module -avoid-version --export-dynamic -nostdlib -shared

if HAVE_PAM
pam_lib = pam_slurm.la
else
pam_lib =
endif

pkglib_LTLIBRARIES = $(pam_lib)

if HAVE_PAM
pam_slurm_la_SOURCES = 				\
	pam_slurm.c				\
	$(top_builddir)/src/common/hostlist.c	\
	$(top_builddir)/src/common/hostlist.h
pam_slurm_la_LDFLAGS = $(PLUGIN_FLAGS)
#
# The following kludge prevents libtool passing of an (unused) "-rpath" 
# argument to cc, which also permits libtool from passing "-nostdlib" to cc. 
# Without this kludge, "-nostdlib" is not passed to cc and the linking process 
# results in duplicate "_init" and "_fini" symbols being defined, which is a 
# fatal error.
am_pam_slurm_la_rpath =

else
EXTRA_pam_slurm_la_SOURCES = pam_slurm.c
endif

