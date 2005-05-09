# $Id$
# Makefile for select/bluegene plugin

CPPFLAGS =  -DBLUEGENE_CONFIG_FILE=\"$(sysconfdir)/bluegene.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic -lm

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(BGL_INCLUDES)

pkglib_LTLIBRARIES = select_bluegene.la

# Blue Gene node selection plugin.
select_bluegene_la_SOURCES = select_bluegene.c \
				bgl_job_place.c bgl_job_place.h \
				bgl_job_run.c bgl_job_run.h \
				bgl_part_info.c bgl_part_info.h \
				bluegene.c bluegene.h \
				state_test.c state_test.h \
				bgl_switch_connections.c \
				partition_sys.c \
				wrap_rm_api.h

select_bluegene_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

select_bluegene_la_LIBADD  = $(top_builddir)/src/partition_allocator/libpartition_allocator.la 

sbin_PROGRAMS = slurm_prolog slurm_epilog

slurm_prolog_LDADD = $(top_builddir)/src/common/libcommon.la \
		$(top_builddir)/src/api/libslurm.la
slurm_epilog_LDADD =  $(top_builddir)/src/common/libcommon.la \
		$(top_builddir)/src/api/libslurm.la
slurm_prolog_SOURCES = slurm_prolog.c
slurm_epilog_SOURCES = slurm_epilog.c
slurm_prolog_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)
slurm_epilog_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)
