# Makefile.am for bridge_linker on a bgq system

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)

# making a .la

noinst_LTLIBRARIES = libbridge_linker.la
libbridge_linker_la_SOURCES   = bridge_linker.cc \
				bridge_helper.cc bridge_helper.h \
				bridge_status.cc bridge_status.h

libbridge_linker_la_LDFLAGS = $(LIB_LDFLAGS)
