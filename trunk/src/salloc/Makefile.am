#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = salloc

salloc_SOURCES = salloc.c opt.c opt.h msg.c msg.h

convenience_libs = \
	$(top_builddir)/src/common/libcommon.la \
	$(top_builddir)/src/api/libslurm.la \
	$(top_builddir)/src/common/libeio.la

salloc_LDADD = \
	$(convenience_libs) 

salloc_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
