# Makefile.am for bluegene_block_allocator

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

#to build the executable
# noinst_PROGRAMS = block_allocator

# block_allocator_SOURCES = block_allocator.c bridge_linker.c \
# 	block_allocator.h bridge_linker.h

# block_allocator_LDADD = \
# 	$(top_builddir)/src/api/libslurm.la


# block_allocator_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS)

# CPPFLAGS = -DBUILD_EXE


# making a .la

if BLUEGENE_LOADED

noinst_LTLIBRARIES = libbluegene_block_allocator.la 
libbluegene_block_allocator_la_SOURCES =    \
	block_allocator.c bridge_linker.c block_allocator.h bridge_linker.h

libbluegene_block_allocator_la_LDFLAGS        = \
	$(LIB_LDFLAGS) -lm    

else
EXTRA_libbluegene_block_allocator_la_SOURCES =    \
	block_allocator.c bridge_linker.c block_allocator.h bridge_linker.h

endif
