# $Id$
# Makefile for auth plugins

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = sched_wiki.la

sched_wiki_la_SOURCES =	../agent.cpp \
			../courier.cpp \
			../dstring.cpp \
			../prefix_courier.cpp \
			../receptionist.cpp \
			wiki_canceljob.cpp \
			wiki_command.cpp \
			wiki_mailbag.cpp \
			wiki_message.cpp \
			wiki_parser.cpp \
			wiki_request.cpp \
			wiki_response.cpp \
			wiki_startjob.cpp \
			wiki_status.cpp \
			wiki_wrapper.cpp

sched_wiki_la_LDFLAGS = $(PLUGIN_FLAGS)
sched_wiki_la_LIBADD = $(top_builddir)/src/api/libslurm.la -lstdc++
