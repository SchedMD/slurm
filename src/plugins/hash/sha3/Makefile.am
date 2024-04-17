# Makefile for hash/sha3 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = hash_sha3.la

# hash SHA3 plugin.
hash_sha3_la_SOURCES = hash_sha3.c

hash_sha3_la_LDFLAGS = $(PLUGIN_FLAGS)
hash_sha3_la_LIBADD = ../common_xkcp/libhash_common_xkcp.la

force:
$(hash_sha3_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
