# $Id$

AUTOMAKE_OPTIONS = foreign

SUBDIRS    = src testsuite doc

EXTRA_DIST =	\
	etc/slurm.conf.example	\
	etc/init.d.slurm	\
	autogen.sh		\
	Make-rpm.mk		\
	slurm.spec		\
	README			\
	DISCLAIMER		\
	COPYING			\
 	META
	
slurmincludedir=$(includedir)/slurm
slurminclude_HEADERS =	\
	slurm/slurm.h 		\
	slurm/slurm_errno.h

MAINTAINERCLEANFILES = 			\
	aclocal.m4 config.guess 	\
	config.h.in config.sub configure install-sh \
	ltconfig ltmain.sh missing mkinstalldirs \
	slurm/slurm.h	\
	stamp-h.in

include $(top_srcdir)/Make-rpm.mk

maintainer-clean-local:
	-(cd $(top_srcdir) && rm -rf autom4te.cache)
	-find . -name "Makefile.in" -exec rm {} \;
	-find $(PACKAGE)-* -exec chmod u+w {} \;
