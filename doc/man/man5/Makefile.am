htmldir = ${prefix}/share/doc/@PACKAGE@-@VERSION@/html

man5_MANS = bluegene.conf.5 \
	cgroup.conf.5 \
	cray.conf.5 \
	gres.conf.5 \
	slurm.conf.5 \
	slurmdbd.conf.5 \
	topology.conf.5 \
	wiki.conf.5

EXTRA_DIST = $(man5_MANS)

if HAVE_MAN2HTML

html_DATA = \
	bluegene.conf.html \
	cgroup.conf.html \
	gres.conf.html \
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
