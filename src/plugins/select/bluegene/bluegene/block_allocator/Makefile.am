# Makefile.am for block_allocator

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

# making a .la

noinst_LTLIBRARIES = libblock_allocator.la
libblock_allocator_la_SOURCES = block_allocator.c block_allocator.h

libblock_allocator_la_LIBADD = ../bridge_linker/libbridge_linker.la

libblock_allocator_la_LDFLAGS = $(LIB_LDFLAGS) -lm

if BLUEGENE_LOADED

#to build the debug executable
noinst_PROGRAMS = wire_test

wire_testSOURCES = wire_test.c block_allocator.h

wire_test_LDADD = libblock_allocator.la \
	$(top_builddir)/src/api/libslurm.o -ldl

wire_test_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS)

endif

total = $(libblock_allocator_la_LDADD) $(top_builddir)/src/api/libslurm.o

force:
$(total) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
