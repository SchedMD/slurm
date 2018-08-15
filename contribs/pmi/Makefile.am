# Makefile for PMI2 client side library.
#

AUTOMAKE_OPTIONS = foreign
AM_CPPFLAGS = -fPIC -I$(top_srcdir) -I$(top_srcdir)/src/common

if WITH_GNU_LD
PMI_VERSION_SCRIPT = \
        pmi_version.map
PMI_OTHER_FLAGS = \
        -Wl,--version-script=$(PMI_VERSION_SCRIPT)
endif

# libpmi version information
#
# The libpmi interface shouldn't be changing any time soon, so for SLURM's
# libpmi only the library REVISION and AGE should change (and it is debatable
# whether these parts of the .so version should change).
#
# REVISION is changed if the source of the library has changed so linkers
# prefer the new version.
# AGE is changed if interfaces are added but compatibility is not broken.
libpmi_current = 0
libpmi_age     = 0
libpmi_rev     = 0

BUILT_SOURCES = $(PMI_VERSION_SCRIPT)
lib_LTLIBRARIES = libpmi.la

libpmi_la_SOURCES = pmi.c
libpmi_la_LIBADD = $(DL_LIBS) $(LIB_SLURM)
libpmi_la_LDFLAGS = $(LIB_LDFLAGS) \
        -version-info $(libpmi_current):$(libpmi_rev):$(libpmi_age) \
        $(PMI_OTHER_FLAGS)

$(PMI_VERSION_SCRIPT) :
	(echo "{ global:"; \
	 echo "   PMI_*;"; \
	 echo "  local: *;"; \
	 echo "};") > $(PMI_VERSION_SCRIPT)

CLEANFILES = \
	$(PMI_VERSION_SCRIPT)

DISTCLEANFILES = \
	$(PMI_VERSION_SCRIPT)

