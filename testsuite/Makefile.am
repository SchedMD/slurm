
AUTOMAKE_OPTIONS = dejagnu
#RUNTESTDEFAULTFLAGS = --srcdir $$srcdir/testsuite

SUBDIRS = expect slurm_unit

noinst_HEADERS = dejagnu.h

DISTCLEANFILES = \
	slurm.sum slurm.log

install-data-hook:
	if [ ! -f $(srcdir)/testsuite.conf ]; then \
		cp -p testsuite.conf.sample $(srcdir)/testsuite.conf; \
	fi

uninstall-hook:
	rm -f $(srcdir)/testsuite.conf
