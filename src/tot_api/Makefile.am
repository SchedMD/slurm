#
# Makefile for tot slurm API library

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

if WITH_CYGWIN
noinst_LTLIBRARIES = libslurmtot.la

libslurmtot_la_SOURCES =

libslurmtot_la_LIBADD = $(top_builddir)/src/common/libcommon.la \
	$(top_builddir)/src/slurmctld/libslurmctld.la	\
	$(top_builddir)/src/slurmd/common/libslurmd_common.la

libslurmtot_la_LDFLAGS = $(LIB_LDFLAGS) $(OTHER_FLAGS)

force:
$(libslurmtot_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
EXTRA_libslurmtot_la_SOURCES =
endif
