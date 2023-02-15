
AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I auxdir
SUBDIRS    = auxdir src testsuite doc etc

pkginclude_HEADERS =		\
	slurm/pmi.h		\
	slurm/slurm.h 		\
	slurm/slurmdb.h 	\
	slurm/slurm_errno.h	\
	slurm/slurm_version.h	\
	slurm/smd_ns.h		\
	slurm/spank.h

MAINTAINERCLEANFILES = 					\
	aclocal.m4 config.guess				\
	config.h.in config.sub configure install-sh 	\
	ltconfig ltmain.sh missing mkinstalldirs 	\
	slurm/slurm_version.h				\
	stamp-h.in

# Cleanup contribs with distclean/mrproper
distclean-contrib:
	@cd contribs && \
	$(MAKE) distclean && \
	cd ..;

distclean-local: distclean-contrib
	-(cd $(top_srcdir) && rm -rf autom4te*.cache autoscan.*)
	-(cd $(top_srcdir) && rm -rf $(PACKAGE)-*)

mrproper: distclean-local clean
	-(cd $(top_srcdir) && rm -rf autom4te.cache config.h config.log)
	-(cd $(top_srcdir) && rm -rf config.status libtool stamp-h1)
	-(cd $(top_srcdir)/auxdir && rm -rf mkinstalldirs)
	-(cd $(top_srcdir)/slurm  && rm -rf stamp-h2 slurm_version.h)
	-find $(top_srcdir)/src -name "Makefile" -exec rm {} \;
	-find $(top_srcdir) -depth -name ".deps" -exec rm -rf {} \;

contrib:
	@cd contribs && \
	$(MAKE) && \
	cd ..;


install-contrib:
	@cd contribs && \
	$(MAKE) DESTDIR=$(DESTDIR) install && \
	cd ..;

clean-contrib:
	@cd contribs && \
	$(MAKE) clean && \
	cd ..;
