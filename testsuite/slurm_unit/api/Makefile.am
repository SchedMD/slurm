AUTOMAKE_OPTIONS = foreign

SUBDIRS = manual
noinst_PROGRAMS = api-test 

INCLUDES = -I$(top_srcdir)/src/common
LDADD   = $(top_srcdir)/src/common/libcommon.la \
		$(top_srcdir)/src/api/libslurm.la
