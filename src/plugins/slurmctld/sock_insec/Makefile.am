# Makefile for slurmctld/sock_insec plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = slurmctld_sock_insec.la

# slurmctld sock_insec plugin
slurmctld_sock_insec_la_SOURCES = \
	read_config.c	\
	read_config.h	\
	sock_comm.c	\
	sock_comm.h	\
	sock_insec.c
slurmctld_sock_insec_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
