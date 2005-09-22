# $Id$
# Makefile for sched/wiki plugins

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

wiki_src =		../agent.cpp		\
			../agent.h		\
			../comparator.h		\
			../condition.h		\
			../courier.cpp		\
			../courier.h		\
			../dstring.cpp		\
			../dstring.h		\
			../mailbag.h		\
			../message.h		\
			../prefix_courier.cpp	\
			../prefix_courier.h	\
			../receptionist.cpp	\
			../receptionist.h	\
			wiki_canceljob.cpp	\
			wiki_command.cpp	\
			wiki_mailbag.cpp	\
			wiki_mailbag.h		\
			wiki_message.cpp	\
			wiki_message.h		\
			wiki_parser.cpp		\
			wiki_parser.h		\
			wiki_request.cpp	\
			wiki_response.cpp	\
			wiki_startjob.cpp	\
			wiki_status.cpp		\
			wiki_wrapper.cpp


if HAVE_AIX
EXTRA_sched_wiki_la_SOURCES = $(wiki_src)
else
pkglib_LTLIBRARIES    = sched_wiki.la
sched_wiki_la_SOURCES = $(wiki_src)
sched_wiki_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
sched_wiki_la_LIBADD  = -lstdc++
endif
