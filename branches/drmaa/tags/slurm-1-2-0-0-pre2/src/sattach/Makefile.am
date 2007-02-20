#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = sattach

sattach_SOURCES = sattach.c opt.h opt.c

convenience_libs = $(top_builddir)/src/api/libslurmhelper.la

sattach_LDADD = \
	$(convenience_libs) 

sattach_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
