# Makefile.am for bridge_linker on a bluegene system

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

# making a .la

noinst_LTLIBRARIES = libbridge_linker.la
libbridge_linker_la_SOURCES = bridge_linker.c bridge_status.c \
			bridge_switch_connections.c

libbridge_linker_la_LDFLAGS = $(LIB_LDFLAGS) -lm
