# Makefile.am for common_as

AUTOMAKE_OPTIONS = foreign

LIBACCOUNTING_STORAGE_COMMON_LA_SOURCES = common_as.c common_as.h

if ! USE_LOADLEVELER

CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libaccounting_storage_common.la
libaccounting_storage_common_la_SOURCES = $(LIBACCOUNTING_STORAGE_COMMON_LA_SOURCES)

else
EXTRA_libaccounting_storage_common_la_SOURCES = $(LIBACCOUNTING_STORAGE_COMMON_LA_SOURCES)
endif
