# Makefile for PMI2 client side library.
#

AUTOMAKE_OPTIONS = foreign

pkginclude_HEADERS = slurm/pmi2.h
noinst_HEADERS = pmi2_util.h

if WITH_GNU_LD
PMI2_VERSION_SCRIPT = \
        pmi2_version.map
PMI2_OTHER_FLAGS = \
        -Wl,--version-script=$(PMI2_VERSION_SCRIPT)
endif

libpmi2_current = 0
libpmi2_age     = 0
libpmi2_rev     = 0

BUILT_SOURCES = $(PMI2_VERSION_SCRIPT)
lib_LTLIBRARIES = libpmi2.la

libpmi2_la_SOURCES =  pmi2_api.c  pmi2_util.c slurm/pmi2.h
libpmi2_la_LDFLAGS = $(LIB_LDFLAGS) -version-info $(libpmi2_current):$(libpmi2_rev):$(libpmi2_age) \
	$(PMI2_OTHER_FLAGS)

$(PMI2_VERSION_SCRIPT) :
	(echo "{ global:"; \
	echo "   PMI2_*;"; \
	echo "   PMIX_*;"; \
	echo "  local: *;"; \
	echo "};") > $(PMI2_VERSION_SCRIPT)

CLEANFILES = \
	$(PMI_VERSION_SCRIPT)

DISTCLEANFILES = \
	$(PMI_VERSION_SCRIPT)

