# Makefile for acct_gather_profile/io_energy plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common -I$(top_srcdir)/src/plugins/acct_gather_profile/common

pkglib_LTLIBRARIES = acct_gather_profile_io_energy.la

# cpu/core energy accounting plugin.
acct_gather_profile_io_energy_la_SOURCES = io_energy.c io_energy.h \
		   ../common/profile_hdf5.c    ../common/profile_hdf5.h

acct_gather_profile_io_energy_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) \
					   $(HDF5_LDFLAGS) $(HDF5_LIBS) 
