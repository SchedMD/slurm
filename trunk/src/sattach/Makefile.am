# $Id: Makefile.am 8423 2006-06-20 17:48:33Z morrone $
#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = sattach

sattach_SOURCES = sattach.c

convenience_libs = \
	$(top_builddir)/src/common/libcommon.la \
	$(top_builddir)/src/api/libslurm.la

sattach_LDADD = \
	$(convenience_libs) 

sattach_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
