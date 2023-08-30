
htmldir = ${datadir}/doc/${PACKAGE}-${SLURM_VERSION_STRING}/html

generated_html = \
	accounting.html \
	add.html \
	api.html \
	big_sys.html \
	burst_buffer.html \
	cgroups.html \
	cgroup_v2.html \
	classic_fair_share.html \
	cli_filter_plugins.html \
	configless_slurm.html \
	cons_res.html \
	cons_res_share.html \
	containers.html \
	contributor.html \
	core_spec.html \
	cpu_management.html \
	cray.html \
	disclaimer.html \
	dist_plane.html \
	documentation.html \
	download.html \
	dynamic_nodes.html \
	elasticsearch.html \
	elastic_computing.html \
	faq.html \
	federation.html \
	gang_scheduling.html \
	gres.html \
	gres_design.html \
	hdf5_profile_user_guide.html \
	heterogeneous_jobs.html \
	high_throughput.html \
	intel_knl.html \
	jobcomp_kafka.html \
	job_array.html \
	job_container_tmpfs.html \
	job_exit_code.html \
	job_launch.html \
	job_submit_plugins.html \
	jwt.html \
	licenses.html \
	fair_tree.html \
	mail.html \
	man_index.html \
	mc_support.html \
	mcs.html \
	meetings.html \
	mpi_guide.html \
	multi_cluster.html \
	network.html \
	news.html \
	nss_slurm.html \
	openapi_release_notes.html \
	overview.html \
	pam_slurm_adopt.html \
	platforms.html \
	plugins.html \
	power_mgmt.html \
	power_save.html \
	preempt.html \
	prep_plugins.html \
	priority_multifactor.html \
	priority_multifactor3.html \
	programmer_guide.html \
	prolog_epilog.html \
	publications.html \
	qos.html \
	quickstart_admin.html \
	quickstart.html \
	reservations.html \
	resource_binding.html \
	resource_limits.html \
	rest.html \
	rest_api.html \
	rosetta.html \
	sched_config.html \
	select_design.html \
	selinux.html \
	site_factor.html \
	slurm.html \
	slurm_ug_agenda.html \
	team.html \
	testimonials.html \
	topology.html \
	tres.html \
	troubleshoot.html \
	tutorials.html \
	user_permissions.html \
	wckey.html

html_DATA = \
	${generated_html} \
	allocation_pies.gif \
	arch.gif \
	coding_style.pdf \
	configurator.html \
	configurator.easy.html \
	cg_hierarchy.jpg \
	entities.gif \
	example_usage.gif \
	fonts.css \
	fonts.ttf \
	hdf5_task_attr.png \
	hdf5_job_outline.png \
	ibm_pe_fig1.png \
	ibm_pe_fig2.png \
	jobcomp_kafka_fig1.png \
	jquery.min.js \
	k_function.gif \
	mc_support.gif \
	network_failover.gif \
	network_federation.gif \
	network_multi_cluster.gif \
	network_srun.gif \
	network_standard.gif \
	node_lifecycle.png \
	plane_ex1.gif \
	plane_ex2.gif \
	plane_ex3.gif \
	plane_ex4.gif \
	plane_ex5.gif \
	plane_ex6.gif \
	plane_ex7.gif \
	reset.css \
	openapi.css \
	slurm_logo.png \
	schedmd.png \
	slurm.css \
	Slurm_Entity.pdf \
	Slurm_Individual.pdf \
	squeue_color.png \
	style.css \
	topo_ex1.gif \
	topo_ex2.gif \
	tutorial_intro_files.tar \
	usage_pies.gif

MOSTLYCLEANFILES = ${generated_html}

SUFFIXES = .html

.shtml.html:
	`dirname $<`/shtml2html.py @SLURM_MAJOR@.@SLURM_MINOR@ $<
