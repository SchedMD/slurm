# $Id$
# Makefile.am for graph_solver

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = partition_allocator

partition_allocator_SOURCES = partition_allocator.c partition_allocator.h \
                       graph_solver.c graph_solver.h \
                       graph_structs.c graph_structs.h 

partition_allocator_LDADD = \
	$(top_builddir)/src/common/libcommon.la \
	$(top_builddir)/src/common/list.o \
	$(top_builddir)/src/common/xmalloc.o

partition_allocator_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

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
