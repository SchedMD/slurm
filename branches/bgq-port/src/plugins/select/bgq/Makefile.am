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
			bluegene.c bluegene.h \
			jobinfo.c jobinfo.h\
			nodeinfo.c nodeinfo.h

select_bgq_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(BG_LDFLAGS)
select_bgq_la_LIBADD  = block_allocator/libblock_allocator.la

force:
$(select_bgq_la_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
# These are needed for pack/unpack of structures for cross-cluster stuff
select_bgq_la_SOURCES = select_bgq.c bgq.h bgq_enums.h
select_bgq_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)


endif
