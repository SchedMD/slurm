# Makefile for scontrol

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common

bin_PROGRAMS = scontrol

LDADD = $(top_srcdir)/src/api/libslurmapi.a $(top_srcdir)/src/common/libcommon.a

scontrol_SOURCES = scontrol.c

