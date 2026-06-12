# Makefile for hash/k12 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = hash_k12.la

# hash K12 plugin.
hash_k12_la_SOURCES =			\
	hash_k12.c

hash_k12_la_LDFLAGS = $(PLUGIN_FLAGS)
hash_k12_la_LIBADD = ../common_xkcp/libhash_common_xkcp.la

force:
$(hash_k12_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
