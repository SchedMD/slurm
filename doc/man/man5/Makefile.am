htmldir = ${datadir}/doc/${PACKAGE}-${SLURM_VERSION_STRING}/html

man5_MANS = \
	acct_gather.conf.5 \
	burst_buffer.conf.5 \
	cgroup.conf.5 \
	ext_sensors.conf.5 \
	gres.conf.5 \
	helpers.conf.5 \
	job_container.conf.5 \
	knl.conf.5 \
	mpi.conf.5 \
	nonstop.conf.5 \
	oci.conf.5 \
	slurm.conf.5 \
	slurmdbd.conf.5 \
	topology.conf.5

if HAVE_MAN2HTML

html_DATA = \
	acct_gather.conf.html \
	burst_buffer.conf.html \
	cgroup.conf.html \
	ext_sensors.conf.html \
	gres.conf.html \
	helpers.conf.html \
	job_container.conf.html \
	knl.conf.html \
	mpi.conf.html \
	nonstop.conf.html \
	oci.conf.html \
	slurm.conf.html \
	slurmdbd.conf.html \
	topology.conf.html

MOSTLYCLEANFILES = ${html_DATA}

SUFFIXES = .html

.5.html:
	`dirname $<`/../man2html.py @SLURM_MAJOR@.@SLURM_MINOR@ $(srcdir)/../../html/header.txt $(srcdir)/../../html/footer.txt $<

endif
