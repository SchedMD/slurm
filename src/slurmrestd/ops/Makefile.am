AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.* *.bin.lo *.bin.o *.jsono

AM_CPPFLAGS = -I$(top_srcdir)

SRCS = api.h api.c \
	   diag.h diag.c \
	   jobs.c jobs.h \
	   nodes.c nodes.h \
	   partitions.c partitions.h

noinst_LTLIBRARIES = libslurmrest_ops.la
libslurmrest_ops_la_SOURCES = $(SRCS)
