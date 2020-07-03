htmldir = ${datadir}/doc/${PACKAGE}-${SLURM_VERSION_STRING}/html

man8_MANS = slurmctld.8 \
	slurmd.8 \
	slurmdbd.8 \
	slurmrestd.8 \
	slurmstepd.8 \
	spank.8

if HAVE_MAN2HTML

html_DATA = \
	slurmctld.html \
	slurmd.html \
	slurmdbd.html \
	slurmrestd.html \
	slurmstepd.html \
	spank.html

MOSTLYCLEANFILES = ${html_DATA}

SUFFIXES = .html

.8.html:
	`dirname $<`/../man2html.py @SLURM_MAJOR@.@SLURM_MINOR@ $(srcdir)/../../html/header.txt $(srcdir)/../../html/footer.txt $<

endif
