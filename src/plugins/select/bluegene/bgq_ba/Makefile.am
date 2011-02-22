# Makefile.am for block_allocator

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

# making a .la

noinst_LTLIBRARIES = libblock_allocator.la
libblock_allocator_la_SOURCES = block_allocator.c block_allocator.h

libblock_allocator_la_LIBADD  = ../bgq_bl/libbridge_linker.la

libblock_allocator_la_LDFLAGS = $(LIB_LDFLAGS) -lm

total = $(libblock_allocator_la_LDADD)

#to build the debug executable
noinst_PROGRAMS = wire_test

wire_test_SOURCES = wire_test.c

wire_test_LDADD = $(top_builddir)/src/api/libslurm.o -ldl \
	../libba_common.la libblock_allocator.la

total += ../libba_common.la $(top_builddir)/src/api/libslurm.o

wire_test_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS) $(BG_LDFLAGS)

# force link with g++
nodist_EXTRA_wire_test_SOURCES = dummy.cxx

force:
$(total) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
