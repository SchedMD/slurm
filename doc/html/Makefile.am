
htmldir = ${datadir}/doc/${PACKAGE}-${SLURM_VERSION_STRING}/html

generated_html = \
	accounting.html \
	accounting_storageplugins.html \
	acct_gather_energy_plugins.html \
	acct_gather_profile_plugins.html \
	add.html \
	api.html \
	authplugins.html \
	big_sys.html \
	bluegene.html \
	cgroups.html \
	checkpoint_blcr.html \
	checkpoint_plugins.html \
	cons_res.html \
	cons_res_share.html \
	contributor.html \
	core_spec.html \
	core_spec_plugins.html \
	cpu_management.html \
	cray.html \
	cray_alps.html \
	crypto_plugins.html \
	disclaimer.html \
	dist_plane.html \
	documentation.html \
	download.html \
	dynalloc.html \
	elastic_computing.html \
	ext_sensorsplugins.html \
	faq.html \
	gang_scheduling.html \
	gres.html \
	gres_design.html \
	gres_plugins.html \
	hdf5_profile_user_guide.html \
	high_throughput.html \
	ibm.html \
	ibm-pe.html \
	jobacct_gatherplugins.html \
	job_array.html \
	job_container_plugins.html \
	job_exit_code.html \
	job_launch.html \
	job_submit_plugins.html \
	jobcompplugins.html \
	launch_plugins.html \
	licenses.html \
	fair_tree.html \
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
	priority_multifactor2.html \
	priority_multifactor3.html \
	priority_plugins.html \
	proctrack_plugins.html \
	programmer_guide.html \
	prolog_epilog.html \
	publications.html \
	qos.html \
	quickstart_admin.html \
	quickstart.html \
	reservations.html \
	resource_limits.html \
	rosetta.html \
	route_plugin.html \
	rpc.html \
	sched_config.html \
	schedplugins.html \
	select_design.html \
	selectplugins.html \
	slurm.html \
	slurmctld_plugstack.html \
	slurm_ug_agenda.html \
	slurm_ug_cfp.html \
	slurm_ug_registration.html \
	sun_const.html \
	switchplugins.html \
	taskplugins.html \
	team.html \
	testimonials.html \
	topology.html \
	topology_plugin.html \
	troubleshoot.html \
	tutorials.html \
	wckey.html

html_DATA = \
	${generated_html} \
	allocation_pies.gif \
	arch.gif \
	bull.jpg \
	coding_style.pdf \
	configurator.html \
	configurator.easy.html \
	entities.gif \
	example_usage.gif \
	hdf5_task_attr.png \
	hdf5_job_outline.png \
	ibm_pe_fig1.png \
	ibm_pe_fig2.png \
	k_function.gif \
	linuxstyles.css \
	lll.gif \
	mc_support.gif \
	plane_ex1.gif \
	plane_ex2.gif \
	plane_ex3.gif \
	plane_ex4.gif \
	plane_ex5.gif \
	plane_ex6.gif \
	plane_ex7.gif \
	slurm_logo.png \
	schedmd.png \
	slurm_design.pdf \
	slurmstyles.css \
	Slurm_Entity.pdf \
	Slurm_Individual.pdf \
	sponsors.gif \
	topo_ex1.gif \
	topo_ex2.gif \
	tutorial_intro_files.tar \
	usage_pies.gif

MOSTLYCLEANFILES = ${generated_html}

EXTRA_DIST = $(html_DATA)

SUFFIXES = .html

.shtml.html:
	`dirname $<`/shtml2html.py @SLURM_MAJOR@.@SLURM_MINOR@ $<
