# $Id$
# Makefile.am for graph_solver

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BGL_INCLUDES)

#to build the executable
# noinst_PROGRAMS = partition_allocator

# partition_allocator_SOURCES = partition_allocator.c partition_allocator.h

# partition_allocator_LDADD = \
# 	$(top_builddir)/src/common/libcommon.la -lpthread \
# 	$(top_builddir)/src/api/libslurm.la

# partition_allocator_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS) $(BGL_LDFLAGS)
# CPPFLAGS = -DBUILD_EXE 

# making a .la
noinst_LTLIBRARIES = libpartition_allocator.la 
libpartition_allocator_la_SOURCES =    \
	partition_allocator.c partition_allocator.h 

libpartition_allocator_la_LIBADD  =  \
	$(top_builddir)/src/common/libcommon.la -lpthread \
	$(top_builddir)/src/api/libslurm.la

libpartition_allocator_la_LDFLAGS        = \
	$(LIB_LDFLAGS) -lm    

libpartition_allocator_la_DEPENDENCIES  = \
	$(top_builddir)/src/common/libcommon.la 
####

# force:
# $(convenience_libs) : force
# 	@cd `dirname $@` && $(MAKE) `basename $@`


#########

# attach.o : attach.c
# 	$(COMPILE) -c -g -o attach.o $(srcdir)/attach.c 

#install-exec-local:
#	umask 022; \
#	if [ -x /usr/lib/rpm/debugedit ]; then \
#	  srcdir=`cd $(top_srcdir) && pwd`; \
#	  /usr/lib/rpm/debugedit -b $$srcdir -d $(pkglibdir) \
#	                         $(DESTDIR)$(bindir)/srun; fi; \
#	$(INSTALL) -D -m 644 $(top_srcdir)/src/srun/srun.wrapper.c \
#	                       $(DESTDIR)$(pkglibdir)/src/srun/srun.wrapper.c
