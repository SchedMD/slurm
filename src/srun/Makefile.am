#

SUBDIRS = libsrun

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/srun/libsrun

bin_PROGRAMS = srun

convenience_libs = $(top_builddir)/src/srun/libsrun/libsrun.la \
	$(top_builddir)/src/api/libslurm.o

srun_SOURCES = \
	srun.c \
	srun_pty.c srun_pty.h \
	srun.wrapper.c
srun_LDADD = $(convenience_libs) $(DL_LIBS)

srun_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

if BGQ_LOADED
# because totalview needs the libraries on load we link in srun
# and not in the launch plugin.
srun_LDFLAGS += $(RUNJOB_LDFLAGS)
endif

force:
$(convenience_libs) : force
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

