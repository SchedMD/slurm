AUTOMAKE_OPTIONS = foreign

noinst_PROGRAMS = security_3_1

TESTS = security_3_1 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

LDADD   = $(top_srcdir)/src/common/libcommon.la \
	$(SSL_LIBS)

force :
$(LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
