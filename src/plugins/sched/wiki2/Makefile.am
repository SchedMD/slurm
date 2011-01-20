# Makefile for wiki2 scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = sched_wiki2.la

sched_wiki2_la_SOURCES = \
	cancel_job.c	\
	crypto.c	\
	crypto.h	\
	event.c		\
	get_jobs.c	\
	get_nodes.c	\
	hostlist.c	\
	initialize.c	\
	job_add_task.c	\
	job_modify.c	\
	job_notify.c	\
	job_release_task.c \
	job_requeue.c	\
	job_signal.c	\
	job_will_run.c	\
	msg.c		\
	msg.h		\
	resume_job.c	\
	sched_wiki.c	\
	start_job.c	\
	suspend_job.c
sched_wiki2_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
