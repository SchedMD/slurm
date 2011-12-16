htmldir = ${prefix}/share/doc/@PACKAGE@-@VERSION@/html

EXTRA_DIST = man8

man8_MANS = slurmctld.8 \
	slurmd.8 \
	slurmdbd.8 \
	slurmstepd.8 \
	spank.8

if HAVE_MAN2HTML
html_DATA = \
	slurmctld.html \
	slurmd.html \
	slurmdbd.html \
	slurmstepd.html \
	spank.html

MOSTLYCLEANFILES = ${html_DATA}

EXTRA_DIST += $(html_DATA)

SUFFIXES = .html

.8.html:
	`dirname $<`/../man2html.py $(srcdir)/../../html/header.txt $(srcdir)/../../html/footer.txt $<

endif

dist-hook:
	-rm -rf `find $(distdir) -name CVS`

