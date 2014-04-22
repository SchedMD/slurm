htmldir = ${datadir}/doc/${PACKAGE}-${SLURM_VERSION_STRING}/html

man5_MANS = \
	acct_gather.conf.5 \
	bluegene.conf.5 \
	cgroup.conf.5 \
	cray.conf.5 \
	ext_sensors.conf.5 \
	gres.conf.5 \
	nonstop.conf.5 \
	slurm.conf.5 \
	slurmdbd.conf.5 \
	topology.conf.5 \
	wiki.conf.5

EXTRA_DIST = $(man5_MANS)

if HAVE_MAN2HTML

html_DATA = \
	acct_gather.conf.html \
	bluegene.conf.html \
	cgroup.conf.html \
	cray.conf.html \
	ext_sensors.conf.html \
	gres.conf.html \
	nonstop.conf.html \
	slurm.conf.html \
	slurmdbd.conf.html \
	topology.conf.html \
	wiki.conf.html

MOSTLYCLEANFILES = ${html_DATA}

EXTRA_DIST += $(html_DATA)

SUFFIXES = .html

.5.html:
	`dirname $<`/../man2html.py @SLURM_MAJOR@.@SLURM_MINOR@ $(srcdir)/../../html/header.txt $(srcdir)/../../html/footer.txt $<

endif
