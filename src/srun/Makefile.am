#

SUBDIRS = libsrun

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/srun/libsrun

bin_PROGRAMS = srun

depend_libs = $(top_builddir)/src/srun/libsrun/libsrun.la \
	$(top_builddir)/src/bcast/libfile_bcast.la


srun_SOURCES = \
	srun.c \
	srun_pty.c srun_pty.h \
	srun.wrapper.c
srun_LDADD = $(depend_libs) $(DL_LIBS) $(LIB_SLURM)

srun_DEPENDENCIES = $(depend_libs) $(LIB_SLURM_BUILD)

srun_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(srun_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

install-exec-local:
	umask 022; \
	if [ -x /usr/lib/rpm/debugedit ]; then \
	  srcdir=`cd $(top_srcdir) && pwd`; \
	  /usr/lib/rpm/debugedit -b $$srcdir -d $(pkglibdir) \
	                         $(DESTDIR)$(bindir)/srun; fi; \
	mkdir -p -m 755 $(DESTDIR)$(pkglibdir)/src/srun
	$(INSTALL) -m 644 $(top_srcdir)/src/srun/srun.wrapper.c \
		$(DESTDIR)$(pkglibdir)/src/srun/srun.wrapper.c

