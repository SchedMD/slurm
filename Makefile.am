
AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I auxdir
SUBDIRS    = auxdir src testsuite doc etc

EXTRA_DIST =			\
	etc/bluegene.conf.example \
	etc/cgroup.conf.example \
	etc/cgroup_allowed_devices_file.conf.example \
	etc/init.d.slurm.in	\
	etc/init.d.slurmdbd.in	\
	etc/layouts.d.power.conf.example \
	etc/layouts.d.power_cpufreq.conf.example \
	etc/layouts.d.unit.conf.example \
	etc/slurm.conf.example	\
	etc/slurmctld.service.in \
	etc/slurmd.service.in	\
	etc/slurmdbd.conf.example \
	etc/slurmdbd.service.in	\
	slurm.spec		\
	README.rst		\
	RELEASE_NOTES		\
	DISCLAIMER		\
	COPYING			\
	AUTHORS			\
	INSTALL			\
	LICENSE.OpenSSL		\
	NEWS			\
	META

pkginclude_HEADERS =		\
	slurm/pmi.h		\
	slurm/slurm.h 		\
	slurm/slurmdb.h 	\
	slurm/slurm_errno.h	\
	slurm/smd_ns.h		\
	slurm/spank.h

MAINTAINERCLEANFILES = 					\
	aclocal.m4 config.guess				\
	config.h.in config.sub configure install-sh 	\
	ltconfig ltmain.sh missing mkinstalldirs 	\
	slurm/slurm.h					\
	stamp-h.in

distclean-local:
	-(cd $(top_srcdir) && rm -rf autom4te*.cache autoscan.*)
	-(cd $(top_srcdir) && rm -rf $(PACKAGE)-*)

mrproper: distclean-local clean
	-(cd $(top_srcdir) && rm -rf autom4te.cache config.h config.log)
	-(cd $(top_srcdir) && rm -rf config.status libtool stamp-h1)
	-(cd $(top_srcdir)/auxdir && rm -rf mkinstalldirs)
	-(cd $(top_srcdir)/slurm  && rm -rf stamp-h2 slurm.h)
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
