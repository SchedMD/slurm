
htmldir = ${prefix}/share/doc/@PACKAGE@-@VERSION@/html

generated_html = \
	accounting.html \
	api.html \
	authplugins.html \
	big_sys.html \
	bluegene.html \
	checkpoint_blcr.html \
	checkpoint_plugins.html \
	cons_res.html \
	cons_res_share.html \
	cray.html \
	crypto_plugins.html \
	dist_plane.html \
	documentation.html \
	download.html \
	faq.html \
	gang_scheduling.html \
	help.html \
	ibm.html \
	jobacct_gatherplugins.html \
	accounting_storageplugins.html \
	jobcompplugins.html \
	mail.html \
	maui.html \
	mc_support.html \
	moab.html \
	mpi_guide.html \
	mpiplugins.html \
	news.html \
	overview.html \
	platforms.html \
	plugins.html \
	power_save.html \
	preempt.html \
	preemption_plugins.html \
	proctrack_plugins.html \
	priority_multifactor.html \
	priority_plugins.html \
	programmer_guide.html \
	publications.html \
	qos.html \
	quickstart_admin.html \
	quickstart.html \
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
	AllocationPies.gif \
	arch.gif \
	configurator.html \
	coding_style.pdf \
	entities.gif \
	ExampleUsage.gif \
	lci.7.tutorial.pdf \
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
	UsagePies.gif

MOSTLYCLEANFILES = ${generated_html}

EXTRA_DIST = $(html_DATA)

SUFFIXES = .html

.shtml.html:
	`dirname $<`/shtml2html.py $<
