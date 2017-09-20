# Makefile for acct_gather_profile/hdf5 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

# Do not put a link to common here.  src/common contains an mpi.h which
# hdf5 could of been installed with a link to the generic mpi.h.
AM_CPPFLAGS = -I$(top_srcdir) $(HDF5_CPPFLAGS)

# cpu/core energy accounting plugin.
HDF5_SOURCES = acct_gather_profile_hdf5.c
HDF5_API_SOURCES = hdf5_api.c hdf5_api.h

if BUILD_HDF5

SUBDIRS = sh5util

pkglib_LTLIBRARIES = acct_gather_profile_hdf5.la
noinst_LTLIBRARIES = libhdf5_api.la

libhdf5_api_la_SOURCES = $(HDF5_API_SOURCES)
libhdf5_api_la_LDFLAGS = $(HDF5_LDFLAGS)
libhdf5_api_la_LIBADD = $(HDF5_LIBS)

acct_gather_profile_hdf5_la_SOURCES = $(HDF5_SOURCES)
acct_gather_profile_hdf5_la_LDFLAGS = \
	$(PLUGIN_FLAGS) $(HDF5_LDFLAGS)
acct_gather_profile_hdf5_la_LIBADD = $(HDF5_LIBS) libhdf5_api.la

else

EXTRA_acct_gather_profile_hdf5_la_SOURCES = $(HDF5_SOURCES)
EXTRA_libhdf5_api_la_SOURCES = $(HDF5_API_SOURCES)

endif
