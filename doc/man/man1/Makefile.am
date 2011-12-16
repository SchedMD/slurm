htmldir = ${prefix}/share/doc/@PACKAGE@-@VERSION@/html

EXTRA_DIST = man1

man1_MANS = \
	sacct.1 \
	sacctmgr.1 \
	salloc.1 \
	sattach.1 \
	sbatch.1 \
	sbcast.1 \
	scancel.1 \
	scontrol.1 \
	sinfo.1   \
	slurm.1 \
	smap.1 \
	sprio.1 \
	squeue.1 \
	sreport.1 \
	srun.1 \
	srun_cr.1 \
	sshare.1 \
	sstat.1 \
	strigger.1 \
	sview.1

if HAVE_MAN2HTML

html_DATA = \
	sacct.html \
	sacctmgr.html \
	salloc.html \
	sattach.html \
	sbatch.html \
	sbcast.html \
	scancel.html \
	scontrol.html \
	sinfo.html \
	smap.html \
	sprio.html \
	squeue.html \
	sreport.html \
	srun.html \
	srun_cr.html \
	sshare.html \
	sstat.html \
	strigger.html \
	sview.html

MOSTLYCLEANFILES = ${html_DATA}

EXTRA_DIST += $(html_DATA)

SUFFIXES = .html

.1.html:
	`dirname $<`/../man2html.py $(srcdir)/../../html/header.txt $(srcdir)/../../html/footer.txt $<

endif

dist-hook:
	-rm -rf `find $(distdir) -name CVS`
