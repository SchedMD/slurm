#
# Makefile for sh5util

AUTOMAKE_OPTIONS = foreign

# Do not put a link to common here.  src/common contains an mpi.h which
# hdf5 could of been installed with a link to the generic mpi.h.
AM_CPPFLAGS = -I$(top_srcdir) -I../ $(HDF5_CPPFLAGS)

SHDF5_SOURCES = sh5util.c

if BUILD_HDF5

bin_PROGRAMS = sh5util

sh5util_SOURCES = $(SHDF5_SOURCES)
sh5util_LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS) \
	../libhdf5_api.la

sh5util_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HDF5_LDFLAGS) $(HDF5_LIBS)

force:
$(sh5util_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_sh5util_SOURCES = $(SHDF5_SOURCES)
endif
