AUTOMAKE_OPTIONS = foreign

check_PROGRAMS = security_3_1

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

LDADD   = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/api/libslurm.la \
	$(SSL_LIBS)

force :
$(LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
