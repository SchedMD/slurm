#
# Makefile for sprfmrgh5

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/plugins/acct_gather_profile/common
bin_PROGRAMS = sprfmrgh5

sprfmrgh5_LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

sprfmrgh5_SOURCES = sprfmrgh5.c sprfmrg5.h	\
		   ../common/profile_hdf5.c    ../common/profile_hdf5.h

force:
$(sprfmrgh5_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sprfmrgh5_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS) $(HDF5_LDFLAGS) $(HDF5_LIBS)

