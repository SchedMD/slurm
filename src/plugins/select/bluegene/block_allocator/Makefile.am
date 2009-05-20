# Makefile.am for bluegene_block_allocator

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

# making a .la

noinst_LTLIBRARIES = libbluegene_block_allocator.la 
libbluegene_block_allocator_la_SOURCES =    \
	block_allocator.c bridge_linker.c block_allocator.h bridge_linker.h

libbluegene_block_allocator_la_LDFLAGS        = \
	$(LIB_LDFLAGS) -lm    

if BLUEGENE_LOADED

#to build the debug executable
noinst_PROGRAMS = wire_test

wire_testSOURCES = wire_test.c block_allocator.h

wire_test_LDADD = libbluegene_block_allocator.la \
	$(top_builddir)/src/api/libslurm.o -ldl

wire_test_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS)

endif
