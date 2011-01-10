# Makefile.am for bgq_block_allocator

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

# making a .la

noinst_LTLIBRARIES = libbgq_block_allocator.la
libbgq_block_allocator_la_SOURCES =    \
	block_allocator.cc bridge_linker.cc block_allocator.h bridge_linker.h

libbgq_block_allocator_la_LDFLAGS        = \
	$(LIB_LDFLAGS) -lm $(BG_LDFLAGS)

if BLUEGENE_LOADED

#to build the debug executable
noinst_PROGRAMS = wire_test

wire_test_SOURCES = wire_test.c block_allocator.h
nodist_EXTRA_wire_test_SOURCES = dummy.cxx

wire_test_LDADD = libbgq_block_allocator.la \
	$(top_builddir)/src/api/libslurm.o -ldl -lpthread

wire_test_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS)

endif
