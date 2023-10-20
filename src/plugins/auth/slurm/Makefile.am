# Makefile for auth/slurm plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(JWT_CPPFLAGS)

pkglib_LTLIBRARIES = auth_slurm.la

# Slurm authentication plugin.
auth_slurm_la_SOURCES = \
	auth_slurm.c \
	auth_slurm.h \
	cred_slurm.c \
	external.c \
	internal.c \
	sack.c \
	sbcast.c \
	util.c
auth_slurm_la_LDFLAGS = $(PLUGIN_FLAGS) $(JWT_LDFLAGS)
auth_slurm_la_LIBADD = $(JWT_LIBS) ../../cred/common/libcred_common.la
