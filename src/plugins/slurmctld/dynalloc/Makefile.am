# Makefile for dynalloc (resource dynamic allocation) plugin

AUTOMAKE_OPTIONS = foreign

AM_CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

AS_DYNALLOC_SOURCES = \
	allocate.c	\
	allocate.h	\
	allocator.c	\
	allocator.h	\
	argv.c		\
	argv.h		\
	deallocate.c \
	deallocate.h \
	info.c	\
	info.h	\
	job_ports_list.c \
	job_ports_list.h \
	msg.c	\
	msg.h	\
	slurmctld_dynalloc.c

if SLURM_ENABLE_DYNAMIC_ALLOCATION

pkglib_LTLIBRARIES = slurmctld_dynalloc.la
slurmctld_dynalloc_la_SOURCES = $(AS_DYNALLOC_SOURCES)
slurmctld_dynalloc_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else

EXTRA_slurmctld_dynalloc_la_SOURCES = $(AS_DYNALLOC_SOURCES)

endif

