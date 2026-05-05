AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

noinst_LTLIBRARIES = libauth_common.la

# Common authentication utilities
libauth_common_la_SOURCES = \
	auth_common.c auth_common.h

force:
$(libauth_common_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
