# $Id$

AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I auxdir
SUBDIRS    = auxdir src testsuite doc 

EXTRA_DIST =			\
	etc/bluegene.conf.example \
	etc/federation.conf.example \
	etc/slurm.conf.example	\
	etc/slurm.epilog.clean	\
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
	META			\
	config.xml

pkginclude_HEADERS =		\
	slurm/pmi.h		\
	slurm/slurm.h 		\
	slurm/slurm_errno.h

MAINTAINERCLEANFILES = 					\
	aclocal.m4 config.guess config.xml		\
	config.h.in config.sub configure install-sh 	\
	ltconfig ltmain.sh missing mkinstalldirs 	\
	slurm/slurm.h					\
	stamp-h.in

distclean-local:
	-(cd $(top_srcdir) && rm -rf autom4te*.cache autoscan.*)
	-(cd $(top_srcdir) && rm -rf $(PACKAGE)-*)

mrproper: distclean-local clean
	-(cd $(top_srcdir) && rm -rf aclocal.m4 autom4te.cache config.h.in)
	-(cd $(top_srcdir) && rm -rf config.log config.status configure)
	-(cd $(top_srcdir) && rm -rf libtool config.h stamp-h1)
	-(cd $(top_srcdir)/auxdir && rm -rf config.guess config.sub ltmain.sh)
	-(cd $(top_srcdir)/auxdir && rm -rf install-sh mkinstalldirs missing)
	-(cd $(top_srcdir)/auxdir && rm -rf depcomp)
	-(cd $(top_srcdir)/slurm  && rm -rf stamp-h2 slurm.h)
	-find $(top_srcdir) -name "Makefile.in" -exec rm {} \;
	-find $(top_srcdir)/src -name "Makefile" -exec rm {} \;
	-find $(top_srcdir) -depth -name ".deps" -exec rm -rf {} \;

