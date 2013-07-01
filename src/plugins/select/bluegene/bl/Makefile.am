# Makefile.am for bridge_linker on a bluegene system

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)

# making a .la

noinst_LTLIBRARIES = libbridge_linker.la
libbridge_linker_la_SOURCES =			\
		bridge_linker.c			\
		bridge_status.c			\
		bridge_status.h			\
		bridge_switch_connections.c	\
		bridge_switch_connections.h

libbridge_linker_la_LDFLAGS = $(LIB_LDFLAGS) -lm
