
htmldir = ${prefix}/share/doc/@PACKAGE@-@VERSION@/html

generated_html = \
	../man/man1/sacct.html \
	../man/man1/sacctmgr.html \
	../man/man1/salloc.html \
	../man/man1/sattach.html \
	../man/man1/sbatch.html \
	../man/man1/sbcast.html \
	../man/man1/scancel.html \
	../man/man1/scontrol.html \
	../man/man1/sinfo.html \
	../man/man1/slurm.html \
	../man/man1/smap.html \
	../man/man1/sprio.html \
	../man/man1/squeue.html \
	../man/man1/sreport.html \
	../man/man1/srun.html \
	../man/man1/srun_cr.html \
	../man/man1/sshare.html \
	../man/man1/sstat.html \
	../man/man1/strigger.html \
	../man/man1/sview.html \
	../man/man5/bluegene.conf.html \
	../man/man5/cgroup.conf.html \
	../man/man5/gres.conf.html \
	../man/man5/slurm.conf.html \
	../man/man5/slurmdbd.conf.html \
	../man/man5/topology.conf.html \
	../man/man5/wiki.conf.html \
	../man/man8/slurmctld.html \
	../man/man8/slurmd.html \
	../man/man8/slurmdbd.html \
	../man/man8/slurmstepd.html \
	../man/man8/spank.html \
	accounting.html \
	accounting_storageplugins.html \
	api.html \
	authplugins.html \
	big_sys.html \
	bluegene.html \
	cfp.html \
	checkpoint_blcr.html \
	checkpoint_plugins.html \
	cons_res.html \
	cons_res_share.html \
	cpu_management.html \
	cray.html \
	crypto_plugins.html \
	dist_plane.html \
	documentation.html \
	download.html \
	faq.html \
	gang_scheduling.html \
	gres.html \
	gres_plugins.html \
	help.html \
	high_throughput.html \
	ibm.html \
	jobacct_gatherplugins.html \
	job_exit_code.html \
	job_submit_plugins.html \
	jobcompplugins.html \
	mail.html \
	man_index.html \
	maui.html \
	mc_support.html \
	meetings.html \
	moab.html \
	mpi_guide.html \
	mpiplugins.html \
	multi_cluster.html \
	news.html \
	overview.html \
	platforms.html \
	plugins.html \
	power_save.html \
	preempt.html \
	preemption_plugins.html \
	priority_multifactor.html \
	priority_plugins.html \
	proctrack_plugins.html \
	programmer_guide.html \
	publications.html \
	qos.html \
	quickstart_admin.html \
	quickstart.html \
	registration.html \
	reservations.html \
	resource_limits.html \
	schedplugins.html \
	selectplugins.html \
	slurm.html \
	sun_const.html \
	switchplugins.html \
	taskplugins.html \
	team.html \
	testimonials.html \
	topology.html \
	topology_plugin.html \
	troubleshoot.html

html_DATA = \
	${generated_html} \
	allocation_pies.gif \
	arch.gif \
	configurator.html \
	coding_style.pdf \
	entities.gif \
	example_usage.gif \
	lll.gif \
	mc_support.gif \
	plane_ex1.gif \
	plane_ex2.gif \
	plane_ex3.gif \
	plane_ex4.gif \
	plane_ex5.gif \
	plane_ex6.gif \
	plane_ex7.gif \
	slurm_banner.gif \
	slurm_design.pdf \
	slurmstyles.css \
	sponsors.gif \
	topo_ex1.gif \
	topo_ex2.gif \
	linuxstyles.css \
	usage_pies.gif

MOSTLYCLEANFILES = ${generated_html}

EXTRA_DIST = $(html_DATA)

SUFFIXES = .html

.shtml.html:
	`dirname $<`/shtml2html.py $<

.1.html:
	$(top_srcdir)/doc/html/man2html.py $<
.5.html:
	$(top_srcdir)/doc/html/man2html.py $<
.8.html:
	$(top_srcdir)/doc/html/man2html.py $<
