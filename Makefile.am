AUTOMAKE_OPTIONS = foreign

SUBDIRS = src testsuite doc

EXTRA_DIST = 	\
	auxdir 	\
	etc 	\
	doc 	\
	autogen.sh

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 config.guess \
	               config.h.in config.sub configure install-sh \
	               ltconfig ltmain.sh missing mkinstalldirs \
		       stamp-h.in

include $(top_srcdir)/Make-rpm.mk
