# $Id$
# Makefile for slurm_tv_clean

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

sbin_PROGRAMS = slurm_tv_clean

LDADD = 					  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/common/libhostlist.la \
	$(top_builddir)/src/api/libslurm.la       \
	$(READLINE_LIBS)


slurm_tv_clean_SOURCES = slurm_tv_clean.c
