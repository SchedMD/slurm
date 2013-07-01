# Makefile.am for libpermapi for poe to interface with

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common \
	   -I$(top_srcdir)/src/srun/libsrun $(NRT_CPPFLAGS)

# making a .la

pkglib_LTLIBRARIES = libpermapi.la

CPPFLAGS = -DMYSELF_SO=\"$(libdir)/slurm/libpermapi.so\"

libpermapi_la_SOURCES = shr_64.c

libpermapi_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(DL_LIBS) -Wl,-Bsymbolic

libpermapi_la_LIBADD = $(top_builddir)/src/srun/libsrun/libsrun.la \
	$(top_builddir)/src/api/libslurmhelper.la

force:
$(libpermapi_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
