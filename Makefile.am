# $Id$

AUTOMAKE_OPTIONS = foreign

SUBDIRS    = slurm src testsuite doc

EXTRA_DIST =	\
	etc/slurm.conf.example	\
	etc/init.d.slurm	\
	autogen.sh	\
	Make-rpm.mk	\
	slurm.spec	\
	README	\
	DISCLAIMER	\
	COPYING	\
 	META

MAINTAINERCLEANFILES = 			\
	aclocal.m4 config.guess 	\
	config.h.in config.sub configure install-sh \
	ltconfig ltmain.sh missing mkinstalldirs \
	stamp-h.in

include $(top_srcdir)/Make-rpm.mk

maintainer-clean-local:
	-(cd $(top_srcdir) && rm -rf autom4te.cache)
	-find . -name "Makefile.in" -exec rm {} \;
	-find $(PACKAGE)-* -exec chmod u+w {} \;
