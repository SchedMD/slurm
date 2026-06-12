# Makefile for slurm curl library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

if WITH_CURL

noinst_LTLIBRARIES = libslurm_curl.la
libslurm_curl_la_SOURCES = slurm_curl.c slurm_curl.h
libslurm_curl_la_LIBADD = $(LIB_CURL)

force:
$(libslurm_curl_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
