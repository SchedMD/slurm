# $Id$
# Makefile.am for bluegene_block_allocator

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

#to build the executable
# noinst_PROGRAMS = partition_allocator

# partition_allocator_SOURCES = partition_allocator.c partition_allocator.h

# partition_allocator_LDADD = \
# 	$(top_builddir)/src/common/libcommon.la -lpthread \
# 	$(top_builddir)/src/api/libslurm.la

# partition_allocator_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS) $(BG_LDFLAGS)

# CPPFLAGS = -DBUILD_EXE


# making a .la
noinst_LTLIBRARIES = libbluegene_block_allocator.la 
libbluegene_block_allocator_la_SOURCES =    \
	block_allocator.c block_allocator.h 

libbluegene_block_allocator_la_LIBADD  =  \
	$(top_builddir)/src/common/libcommon.la -lpthread \
	$(top_builddir)/src/api/libslurm.la

libbluegene_block_allocator_la_LDFLAGS        = \
	$(LIB_LDFLAGS) -lm    

libbluegene_block_allocator_la_DEPENDENCIES  = \
	$(top_builddir)/src/common/libcommon.la 
