# $Id$

AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I auxdir
SUBDIRS    = auxdir src testsuite doc 

EXTRA_DIST =			\
	Make-rpm.mk		\
	etc/bluegene.conf.example \
	etc/slurm.conf.example	\
	etc/init.d.slurm	\
	etc/ptrace.patch	\
	autogen.sh		\
	slurm.spec		\
	README			\
	DISCLAIMER		\
	COPYING			\
	AUTHORS			\
	INSTALL			\
	NEWS			\
	ChangeLog		\
	META
	
pkginclude_HEADERS =		\
	slurm/bnr.h		\
	slurm/slurm.h 		\
	slurm/slurm_errno.h

MAINTAINERCLEANFILES = 					\
	aclocal.m4 config.guess 			\
	config.h.in config.sub configure install-sh 	\
	ltconfig ltmain.sh missing mkinstalldirs 	\
	slurm/slurm.h					\
	stamp-h.in

include $(top_srcdir)/Make-rpm.mk

maintainer-clean-local:
	-(cd $(top_srcdir) && rm -rf autom4te.cache)
	-find . -name "Makefile.in" -exec rm {} \;

distclean-local:
	-rm -rf autom4te*.cache autoscan.*
	-find $(PACKAGE)-* -exec chmod u+w {} \;
	-rm -rf $(PACKAGE)-*

