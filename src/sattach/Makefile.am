#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sattach

sattach_SOURCES = \
	attach.c attach.h \
	opt.c opt.h \
	sattach.c \
	sattach.wrapper.c

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl

sattach_LDADD = \
	$(convenience_libs)

sattach_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

# debugging information is required for symbols in the attach
# module so that a debugger can attach to spawned tasks
attach.o : attach.c
	$(COMPILE) -c -g -o attach.o $(srcdir)/attach.c

install-exec-local:
	umask 022; \
	if [ -x /usr/lib/rpm/debugedit ]; then \
		srcdir=`cd $(top_srcdir) && pwd`; \
		/usr/lib/rpm/debugedit -b $$srcdir -d $(pkglibdir) \
			$(DESTDIR)$(bindir)/sattach; fi; \
	mkdir -p -m 755 $(DESTDIR)$(pkglibdir)/src/sattach
	$(INSTALL) -m 644 $(top_srcdir)/src/sattach/sattach.wrapper.c \
		$(DESTDIR)$(pkglibdir)/src/sattach/sattach.wrapper.c
