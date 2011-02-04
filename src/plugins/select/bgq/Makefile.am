# Makefile for select/bgq plugin

SUBDIRS = bridge_linker block_allocator

CPPFLAGS = -DBLUEGENE_CONFIG_FILE=\"$(sysconfdir)/bluegene.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(BG_INCLUDES)

pkglib_LTLIBRARIES = select_bgq.la

if BGQ_LOADED

# BGQ node selection plugin.
select_bgq_la_SOURCES = select_bgq.c bg_enums.h bg_structs.h \
			bg_job_place.c bg_job_place.h \
			bg_job_run.c bg_job_run.h \
			bg_record_functions.c bg_record_functions.h \
			block_sys.c \
			bluegene.c bluegene.h \
			defined_block.c defined_block.h \
			dynamic_block.c dynamic_block.h \
			jobinfo.c jobinfo.h\
			nodeinfo.c nodeinfo.h
# force link with g++
nodist_EXTRA_select_bgq_la_SOURCES = dummy.cxx

select_bgq_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(BG_LDFLAGS)

select_bgq_la_LIBADD  = block_allocator/libblock_allocator.la

sbin_PROGRAMS = slurm_prolog slurm_epilog

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl

slurm_prolog_LDADD = $(convenience_libs)
slurm_prolog_SOURCES = slurm_prolog.c
slurm_prolog_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

slurm_epilog_LDADD = $(convenience_libs)
slurm_epilog_SOURCES = slurm_epilog.c
slurm_epilog_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(select_bgq_la_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
# These are needed for pack/unpack of structures for cross-cluster stuff
select_bgq_la_SOURCES = select_bgq.c bgq.h bgq_enums.h
select_bgq_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

endif
