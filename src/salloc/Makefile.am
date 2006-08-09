#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = salloc

salloc_SOURCES = salloc.c salloc.h opt.c opt.h msg.c msg.h

convenience_libs = $(top_builddir)/src/api/libslurmhelper.la

salloc_LDADD = \
	$(convenience_libs) 

salloc_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
