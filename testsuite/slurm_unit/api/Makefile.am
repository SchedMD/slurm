AUTOMAKE_OPTIONS = foreign

SUBDIRS = manual

check_PROGRAMS = $(TESTS)

TESTS = \
	api-test 

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

LDADD   = $(top_builddir)/src/common/libcommon.la \
	  $(top_builddir)/src/api/libslurm.la
