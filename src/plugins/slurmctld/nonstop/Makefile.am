# Makefile for slurmctld/nonstop plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

if WITH_MUNGE
MUNGE = slurmctld_nonstop.la
endif

pkglib_LTLIBRARIES = $(MUNGE)

# slurmctld nonstop plugin
slurmctld_nonstop_la_SOURCES = \
	do_work.c	\
	do_work.h	\
	msg.c		\
	msg.h		\
	nonstop.c	\
	read_config.c	\
	read_config.h

slurmctld_nonstop_la_LDFLAGS = $(PLUGIN_FLAGS) $(MUNGE_LDFLAGS)
slurmctld_nonstop_la_LIBADD =  $(MUNGE_LIBS)
