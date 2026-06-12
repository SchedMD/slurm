# Makefile for hash/common_xkcp

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

noinst_LTLIBRARIES = libhash_common_xkcp.la
libhash_common_xkcp_la_SOURCES =	\
	align.h				\
	brg_endian.h			\
	config-xkcp.h			\
	KangarooTwelve.c		\
	KangarooTwelve.h		\
	KeccakHash.c			\
	KeccakHash.h			\
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
