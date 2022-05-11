# Makefile for hash/k12 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = hash_k12.la

# hash K12 plugin.
hash_k12_la_SOURCES =			\
	align.h				\
	brg_endian.h			\
	hash_k12.c			\
	k12-config.h			\
	KangarooTwelve.c		\
	KangarooTwelve.h		\
	KeccakP-1600-64.macros		\
	KeccakP-1600-opt64.c		\
	KeccakP-1600-opt64-config.h	\
	KeccakP-1600-SnP.h		\
	KeccakP-1600-unrolling.macros	\
	KeccakSponge.c			\
	KeccakSponge.h			\
	KeccakSponge.inc		\
	Phases.h			\
	SnP-Relaned.h

hash_k12_la_LDFLAGS = $(PLUGIN_FLAGS)

force:
$(hash_k12_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
