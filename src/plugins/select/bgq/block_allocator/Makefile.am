# Makefile.am for block_allocator

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

# making a .la

noinst_LTLIBRARIES = libblock_allocator.la
libblock_allocator_la_SOURCES =    \
	block_allocator.c block_allocator.h

libblock_allocator_la_LDFLAGS        = \
	$(LIB_LDFLAGS) -lm

libblock_allocator_la_LIBADD  = ../bridge_linker/libbridge_linker.la

if BGQ_LOADED

#to build the debug executable
noinst_PROGRAMS = wire_test

wire_test_SOURCES = wire_test.c block_allocator.h
# This extra file is needed to force using g++ to link.  (Pretty slick huh)
# g++ is needed when compiling main programs to c++ stuff.
nodist_EXTRA_wire_test_SOURCES = dummy.cxx

wire_test_LDADD = libblock_allocator.la \
	$(top_builddir)/src/api/libslurm.o -ldl -lpthread


wire_test_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS) $(BG_LDFLAGS)

endif

total = $(libblock_allocator_la_LDADD) $(wire_test_LDADD)

force:
$(total) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
