# Makefile for select/bluegene plugin

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

PLUGIN_FLAGS = -module -avoid-version --export-dynamic -lm

AM_CPPFLAGS = -I$(top_srcdir)  -I$(top_srcdir)/src/common $(BG_INCLUDES)

pkglib_LTLIBRARIES = select_bluegene.la

noinst_LTLIBRARIES = libba_common.la libconfigure_api.la

convenience_libs = $(top_builddir)/src/api/libslurmhelper.la \
	-lpthread $(DL_LIBS)

# These are needed for pack/unpack of structures for cross-cluster stuff
select_bluegene_la_SOURCES = select_bluegene.c \
			bg_job_info.c bg_job_info.h \
			bg_node_info.c bg_node_info.h

libba_common_la_SOURCES = ba_common.c ba_common.h

libconfigure_api_la_SOURCES = configure_api.c configure_api.h

select_bluegene_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
select_bluegene_la_LIBADD = libba_common.la

if BLUEGENE_LOADED

SUBDIRS = sfree

if BG_L_P_LOADED
SUBDIRS += bl ba

if BGL_LOADED
pkglib_LTLIBRARIES += libsched_if64.la
libsched_if64_la_SOURCES = libsched_if64.c
libsched_if64_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
else
pkglib_LTLIBRARIES += libsched_if.la
libsched_if_la_SOURCES = libsched_if64.c
libsched_if_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
endif

block_allocator_lib  = ba/libblock_allocator.la
endif

if BGQ_LOADED
  SUBDIRS += bl_bgq ba_bgq

  block_allocator_lib = ba_bgq/libblock_allocator.la

  # force link with g++
  nodist_EXTRA_select_bluegene_la_SOURCES = dummy.cxx

  pkglib_LTLIBRARIES += runjob_plugin.la
  runjob_plugin_la_SOURCES = runjob_plugin.cc
  runjob_plugin_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
  runjob_plugin_la_LIBADD = $(convenience_libs)
endif

select_bluegene_la_SOURCES +=			\
			bg_core.c		\
			bg_core.h		\
			bg_defined_block.c	\
			bg_defined_block.h	\
			bg_dynamic_block.c	\
			bg_dynamic_block.h	\
			bg_enums.h		\
			bg_job_place.c		\
			bg_job_place.h		\
			bg_job_run.c		\
			bg_job_run.h		\
			bg_list_functions.c	\
			bg_list_functions.h	\
			bg_read_config.c	\
			bg_read_config.h	\
			bg_record_functions.c	\
			bg_record_functions.h	\
			bg_status.c		\
			bg_status.h		\
			bg_structs.h		\
			bridge_linker.h

select_bluegene_la_LIBADD += $(block_allocator_lib) $(BG_LDFLAGS)


sbin_PROGRAMS = slurm_prolog slurm_epilog

slurm_prolog_LDADD = $(convenience_libs)
slurm_prolog_SOURCES = slurm_prolog.c
slurm_prolog_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

slurm_epilog_LDADD = $(convenience_libs)
slurm_epilog_SOURCES = slurm_epilog.c
slurm_epilog_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(block_allocator_lib) $(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
