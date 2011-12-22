# Makefile for wiki scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

SCHED_WIKI_LA_SOURCES = \
	cancel_job.c	\
	crypto.c	\
	crypto.h	\
	get_jobs.c	\
	get_nodes.c	\
	hostlist.c	\
	job_modify.c	\
	msg.c		\
	msg.h		\
	resume_job.c	\
	sched_wiki.c	\
	start_job.c	\
	suspend_job.c

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = sched_wiki.la
sched_wiki_la_SOURCES = $(SCHED_WIKI_LA_SOURCES)
sched_wiki_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_sched_wiki_la_SOURCES = $(SCHED_WIKI_LA_SOURCES)
endif
