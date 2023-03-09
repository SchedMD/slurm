/*****************************************************************************\
 * src/slurmd/common/slurmstepd_init.c - slurmstepd intialization code
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/slurmd/common/slurmstepd_init.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"

/* Assume that the slurmd and slurmstepd are the same version level when slurmd
 * starts slurmstepd, so we do not need to support different protocol versions
 * for the different message formats. */
extern void pack_slurmd_conf_lite(slurmd_conf_t *conf, buf_t *buffer)
{
	xassert(conf != NULL);
	pack16(SLURM_PROTOCOL_VERSION, buffer);

	packstr(conf->hostname, buffer);
	pack16(conf->cpus, buffer);
	pack16(conf->boards, buffer);
	pack16(conf->sockets, buffer);
	pack16(conf->cores, buffer);
	pack16(conf->threads, buffer);
	packstr(conf->cpu_spec_list, buffer);
	pack16(conf->core_spec_cnt, buffer);
	pack64(conf->mem_spec_limit, buffer);
	pack64(conf->conf_memory_size, buffer);
	pack16(conf->block_map_size, buffer);
	pack16_array(conf->block_map, conf->block_map_size, buffer);
	pack16_array(conf->block_map_inv, conf->block_map_size, buffer);
	packstr(conf->spooldir, buffer);
	packstr(conf->node_name, buffer);
	packstr(conf->logfile, buffer);
	pack32(conf->debug_level, buffer);
	pack32(conf->syslog_debug, buffer);
	packbool(conf->daemonize, buffer);
	packstr(conf->node_topo_addr, buffer);
	packstr(conf->node_topo_pattern, buffer);
	pack16(conf->port, buffer);
}

extern int unpack_slurmd_conf_lite_no_alloc(slurmd_conf_t *conf, buf_t *buffer)
{
	uint32_t uint32_tmp;
	uint16_t protocol_version;

	safe_unpack16(&protocol_version, buffer);

	/*
	 * No cross-version support is required here. slurmd and slurmstepd
	 * must always be on the same release.
	 */
	if (protocol_version >= SLURM_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&conf->hostname, &uint32_tmp, buffer);
		safe_unpack16(&conf->cpus, buffer);
		safe_unpack16(&conf->boards, buffer);
		safe_unpack16(&conf->sockets, buffer);
		safe_unpack16(&conf->cores, buffer);
		safe_unpack16(&conf->threads, buffer);
		safe_unpackstr_xmalloc(&conf->cpu_spec_list, &uint32_tmp,
				       buffer);
		safe_unpack16(&conf->core_spec_cnt, buffer);
		safe_unpack64(&conf->mem_spec_limit, buffer);
		safe_unpack64(&conf->conf_memory_size, buffer);
		safe_unpack16(&conf->block_map_size, buffer);
		safe_unpack16_array(&conf->block_map, &uint32_tmp, buffer);
		safe_unpack16_array(&conf->block_map_inv,  &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->spooldir,    &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->node_name,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->logfile,     &uint32_tmp, buffer);
		safe_unpack32(&conf->debug_level, buffer);
		safe_unpack32(&conf->syslog_debug, buffer);
		safe_unpackbool(&conf->daemonize, buffer);
		safe_unpackstr_xmalloc(&conf->node_topo_addr, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->node_topo_pattern, &uint32_tmp, buffer);
		safe_unpack16(&conf->port, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("unpack_error in unpack_slurmd_conf_lite_no_alloc: %m");
	xfree(conf->hostname);
	xfree(conf->cpu_spec_list);
	xfree(conf->spooldir);
	xfree(conf->node_name);
	xfree(conf->logfile);
	xfree(conf->node_topo_addr);
	xfree(conf->node_topo_pattern);
	return SLURM_ERROR;
}

/*
 * Assume that the slurmd and slurmstepd are the same version level when slurmd
 * starts slurmstepd, so we do not need to support different protocol versions
 * for the different message formats.
 */
extern void pack_slurm_conf_lite(buf_t *buffer)
{
	/* last_update */
	/* accounting_storage_tres */
	/* accounting_storage_enforce */
	/* accounting_storage_backup_host */
	/* accounting_storage_ext_host */
	/* accounting_storage_host */
	/* accounting_storage_params */
	/* accounting_storage_pass */
	/* accounting_storage_port */
	/* accounting_storage_type */
	/* accounting_storage_user */
	/* acct_gather_conf */
	packstr(slurm_conf.acct_gather_energy_type, buffer);
	packstr(slurm_conf.acct_gather_profile_type, buffer);
	packstr(slurm_conf.acct_gather_interconnect_type, buffer);
	packstr(slurm_conf.acct_gather_filesystem_type, buffer);
	pack16(slurm_conf.acct_gather_node_freq, buffer);
	packstr(slurm_conf.authalttypes, buffer);
	packstr(slurm_conf.authinfo, buffer);
	packstr(slurm_conf.authalt_params, buffer);
	packstr(slurm_conf.authtype, buffer);
	/* batch_start_timeout */
	/* bb_type */
	/* bcast_exclude */
	/* bcast_parameters */
	/* boot_time */
	/* cgroup_conf */
	/* cli_filter_plugins */
	packstr(slurm_conf.core_spec_plugin, buffer);
	packstr(slurm_conf.cluster_name, buffer);
	packstr(slurm_conf.comm_params, buffer);
	/* complete_wait */
	pack32(slurm_conf.conf_flags, buffer);
	packstr_array(slurm_conf.control_addr, slurm_conf.control_cnt, buffer);
	/* control_cnt */
	/* *control_machine */
	pack32(slurm_conf.cpu_freq_def, buffer);
	pack32(slurm_conf.cpu_freq_govs, buffer);
	packstr(slurm_conf.cred_type, buffer);
	pack64(slurm_conf.debug_flags, buffer);
	/* def_mem_per_cpu */
	/* dependency_params */
	/* eio_timeout */
	/* enforce_part_limits */
	/* epilog */
	/* epilog_msg_time */
	/* epilog_slurmctld */
	/* ext_sensors_type */
	/* ext_sensors_freq */
	/* ext_sensors_conf */
	/* fed_params */
	/* first_job_id */
	/* fs_dampening_factor */
	/* get_env_timeout */
	packstr(slurm_conf.gres_plugins, buffer);
	/* group_time */
	/* group_force */
	packstr(slurm_conf.gpu_freq_def, buffer);
	/* hash_val */
	/* health_check_interval */
	/* health_check_node_state */
	/* health_check_program */
	/* inactive_limit */
	/* interactive_step_opts */
	packstr(slurm_conf.job_acct_gather_freq, buffer);
	packstr(slurm_conf.job_acct_gather_type, buffer);
	packstr(slurm_conf.job_acct_gather_params, buffer);
	pack16(slurm_conf.job_acct_oom_kill, buffer);
	/* job_comp_host */
	/* job_comp_loc */
	/* job_comp_params */
	/* job_comp_pass */
	/* job_comp_port */
	/* job_comp_type */
	/* job_comp_user */
	packstr(slurm_conf.job_container_plugin, buffer);
	/* job_credential_private_key */
	/* job_credential_public_certificate */
	/* job_defaults_list */
	pack16(slurm_conf.job_file_append, buffer);
	/* job_requeue */
	/* job_submit_plugins */
	pack32(slurm_conf.keepalive_interval, buffer);
	pack32(slurm_conf.keepalive_probes, buffer);
	pack32(slurm_conf.keepalive_time, buffer);
	/* kill_on_bad_exit */
	pack16(slurm_conf.kill_wait, buffer);
	packstr(slurm_conf.launch_params, buffer);
	/* licenses */
	pack16(slurm_conf.log_fmt, buffer);
	/* mail_domain */
	/* mail_prog */
	/* max_array_sz */
	/* max_batch_requeue */
	/* max_dbd_msgs */
	/* max_job_cnt */
	/* max_job_id */
	/* max_mem_per_cpu */
	/* max_node_cnt */
	/* max_step_cnt */
	/* max_tasks_per_node */
	/* mcs_plugin */
	/* mcs_plugin_params */
	/* min_job_age */
	/* mpi_conf */
	packstr(slurm_conf.mpi_default, buffer);
	/* mpi_params */
	pack16(slurm_conf.msg_timeout, buffer);
	/* next_job_id */
	/* node_features_conf */
	/* node_features_plugins */
	/* node_prefix */
	/* over_time_limit */
	packstr(slurm_conf.plugindir, buffer);
	packstr(slurm_conf.plugstack, buffer);
	/* power_parameters */
	/* power_plugin */
	/* preempt_exempt_time */
	/* preempt_mode */
	/* preempt_type */
	packstr(slurm_conf.prep_params, buffer);
	packstr(slurm_conf.prep_plugins, buffer);
	/* priority_decay_hl */
	/* priority_calc_period */
	/* priority_favor_small */
	/* priority_flags */
	/* priority_max_age */
	/* priority_params */
	/* priority_reset_period */
	/* priority_type */
	/* priority_weight_age */
	/* priority_weight_assoc */
	/* priority_weight_fs */
	/* priority_weight_js */
	/* priority_weight_part */
	/* priority_weight_qos */
	/* priority_weight_tres */
	/* private_data */
	packstr(slurm_conf.proctrack_type, buffer);
	/* prolog */
	/* prolog_epilog_timeout */
	/* prolog_slurmctld */
	pack16(slurm_conf.propagate_prio_process, buffer);
	/* prolog_flags */
	packstr(slurm_conf.propagate_rlimits, buffer);
	packstr(slurm_conf.propagate_rlimits_except, buffer);
	/* reboot_program */
	/* reconfig_flags */
	/* requeue_exit */
	/* requeue_exit_hold */
	/* resume_fail_program */
	/* resume_program */
	/* resume_rate */
	/* resume_timeout */
	/* resv_epilog */
	/* resv_over_run */
	/* resv_prolog */
	/* ret2service */
	/* route_plugin */
	/* sched_logfile */
	/* sched_log_level */
	/* sched_params */
	/* sched_time_slice */
	/* schedtype */
	/* scron_params */
	packstr(slurm_conf.select_type, buffer);
	/* select_conf_key_pairs */
	pack16(slurm_conf.select_type_param, buffer);
	/* site_factor_plugin */
	/* site_factor_params */
	/* slurm_conf */
	pack32(slurm_conf.slurm_user_id, buffer);
	/* slurm_user_name */
	pack32(slurm_conf.slurmd_user_id, buffer);
	/* slurmd_user_name */
	packstr(slurm_conf.slurmctld_addr, buffer);
	/* slurmctld_debug */
	/* slurmctld_logfile */
	/* slurmctld_pidfile */
	/* slurmctld_plugstack */
	/* slurmctld_plugstack_conf */
	pack32(slurm_conf.slurmctld_port, buffer);
	pack16(slurm_conf.slurmctld_port_count, buffer);
	/* slurmctld_primary_off_prog */
	/* slurmctld_primary_on_prog */
	/* slurmctld_syslog_debug */
	/* slurmctld_timeout */
	/* slurmctld_params */
	/* slurmd_debug */
	/* slurmd_logfile */
	/* slurmd_params */
	/* slurmd_pidfile */
	/* slurmd_port */
	packstr(slurm_conf.slurmd_spooldir, buffer);
	/* slurmd_syslog_debug */
	/* slurmd_timeout */
	/* srun_epilog */
	/* srun_port_range */
	/* srun_prolog */
	/* state_save_location */
	/* suspend_exc_nodes */
	/* suspend_exc_parts */
	/* suspend_program */
	/* suspend_rate */
	/* suspend_time */
	/* suspend_timeout */
	packstr(slurm_conf.switch_type, buffer);
	packstr(slurm_conf.switch_param, buffer);
	packstr(slurm_conf.task_epilog, buffer);
	packstr(slurm_conf.task_plugin, buffer);
	pack32(slurm_conf.task_plugin_param, buffer);
	packstr(slurm_conf.task_prolog, buffer);
	pack16(slurm_conf.tcp_timeout, buffer);
	packstr(slurm_conf.tmp_fs, buffer);
	/* topology_param */
	/* topology_plugin */
	pack16(slurm_conf.tree_width, buffer);
	packstr(slurm_conf.unkillable_program, buffer);
	pack16(slurm_conf.unkillable_timeout, buffer);
	/* version */
	pack16(slurm_conf.vsize_factor, buffer);
	pack16(slurm_conf.wait_time, buffer);
	packstr(slurm_conf.x11_params, buffer);
}

extern int unpack_slurm_conf_lite_no_alloc(buf_t *buffer)
{
	init_slurm_conf(&slurm_conf);
	/* last_update */
	/* accounting_storage_tres */
	/* accounting_storage_enforce */
	/* accounting_storage_backup_host */
	/* accounting_storage_ext_host */
	/* accounting_storage_host */
	/* accounting_storage_params */
	/* accounting_storage_pass */
	/* accounting_storage_port */
	/* accounting_storage_type */
	/* accounting_storage_user */
	/* acct_gather_conf */
	safe_unpackstr(&slurm_conf.acct_gather_energy_type, buffer);
	safe_unpackstr(&slurm_conf.acct_gather_profile_type, buffer);
	safe_unpackstr(&slurm_conf.acct_gather_interconnect_type, buffer);
	safe_unpackstr(&slurm_conf.acct_gather_filesystem_type, buffer);
	safe_unpack16(&slurm_conf.acct_gather_node_freq, buffer);
	safe_unpackstr(&slurm_conf.authalttypes, buffer);
	safe_unpackstr(&slurm_conf.authinfo, buffer);
	safe_unpackstr(&slurm_conf.authalt_params, buffer);
	safe_unpackstr(&slurm_conf.authtype, buffer);
	/* batch_start_timeout */
	/* bb_type */
	/* bcast_exclude */
	/* bcast_parameters */
	/* boot_time */
	/* cgroup_conf */
	/* cli_filter_plugins */
	safe_unpackstr(&slurm_conf.core_spec_plugin, buffer);
	safe_unpackstr(&slurm_conf.cluster_name, buffer);
	safe_unpackstr(&slurm_conf.comm_params, buffer);
	/* complete_wait */
	safe_unpack32(&slurm_conf.conf_flags, buffer);
	safe_unpackstr_array(&slurm_conf.control_addr,
			     &slurm_conf.control_cnt, buffer);
	/* *control_addr */
	/* control_cnt */
	/* *control_machine */
	safe_unpack32(&slurm_conf.cpu_freq_def, buffer);
	safe_unpack32(&slurm_conf.cpu_freq_govs, buffer);
	safe_unpackstr(&slurm_conf.cred_type, buffer);
	safe_unpack64(&slurm_conf.debug_flags, buffer);
	/* def_mem_per_cpu */
	/* dependency_params */
	/* eio_timeout */
	/* enforce_part_limits */
	/* epilog */
	/* epilog_msg_time */
	/* epilog_slurmctld */
	/* ext_sensors_type */
	/* ext_sensors_freq */
	/* ext_sensors_conf */
	/* fed_params */
	/* first_job_id */
	/* fs_dampening_factor */
	/* get_env_timeout */
	safe_unpackstr(&slurm_conf.gres_plugins, buffer);
	/* group_time */
	/* group_force */
	safe_unpackstr(&slurm_conf.gpu_freq_def, buffer);
	/* hash_val */
	/* health_check_interval */
	/* health_check_node_state */
	/* health_check_program */
	/* inactive_limit */
	/* interactive_step_opts */
	safe_unpackstr(&slurm_conf.job_acct_gather_freq, buffer);
	safe_unpackstr(&slurm_conf.job_acct_gather_type, buffer);
	safe_unpackstr(&slurm_conf.job_acct_gather_params, buffer);
	safe_unpack16(&slurm_conf.job_acct_oom_kill, buffer);
	/* job_comp_host */
	/* job_comp_loc */
	/* job_comp_params */
	/* job_comp_pass */
	/* job_comp_port */
	/* job_comp_type */
	/* job_comp_user */
	safe_unpackstr(&slurm_conf.job_container_plugin, buffer);
	/* job_credential_private_key */
	/* job_credential_public_certificate */
	/* job_defaults_list */
	safe_unpack16(&slurm_conf.job_file_append, buffer);
	/* job_requeue */
	/* job_submit_plugins */
	safe_unpack32(&slurm_conf.keepalive_interval, buffer);
	safe_unpack32(&slurm_conf.keepalive_probes, buffer);
	safe_unpack32(&slurm_conf.keepalive_time, buffer);
	/* kill_on_bad_exit */
	safe_unpack16(&slurm_conf.kill_wait, buffer);
	safe_unpackstr(&slurm_conf.launch_params, buffer);
	/* licenses */
	safe_unpack16(&slurm_conf.log_fmt, buffer);
	/* mail_domain */
	/* mail_prog */
	/* max_array_sz */
	/* max_batch_requeue */
	/* max_dbd_msgs */
	/* max_job_cnt */
	/* max_job_id */
	/* max_mem_per_cpu */
	/* max_node_cnt */
	/* max_step_cnt */
	/* max_tasks_per_node */
	/* mcs_plugin */
	/* mcs_plugin_params */
	/* min_job_age */
	/* mpi_conf */
	safe_unpackstr(&slurm_conf.mpi_default, buffer);
	/* mpi_params */
	safe_unpack16(&slurm_conf.msg_timeout, buffer);
	/* next_job_id */
	/* node_features_conf */
	/* node_features_plugins */
	/* node_prefix */
	/* over_time_limit */
	safe_unpackstr(&slurm_conf.plugindir, buffer);
	safe_unpackstr(&slurm_conf.plugstack, buffer);
	/* power_parameters */
	/* power_plugin */
	/* preempt_exempt_time */
	/* preempt_mode */
	/* preempt_type */
	safe_unpackstr(&slurm_conf.prep_params, buffer);
	safe_unpackstr(&slurm_conf.prep_plugins, buffer);
	/* priority_decay_hl */
	/* priority_calc_period */
	/* priority_favor_small */
	/* priority_flags */
	/* priority_max_age */
	/* priority_params */
	/* priority_reset_period */
	/* priority_type */
	/* priority_weight_age */
	/* priority_weight_assoc */
	/* priority_weight_fs */
	/* priority_weight_js */
	/* priority_weight_part */
	/* priority_weight_qos */
	/* priority_weight_tres */
	/* private_data */
	safe_unpackstr(&slurm_conf.proctrack_type, buffer);
	/* prolog */
	/* prolog_epilog_timeout */
	/* prolog_slurmctld */
	safe_unpack16(&slurm_conf.propagate_prio_process, buffer);
	/* prolog_flags */
	safe_unpackstr(&slurm_conf.propagate_rlimits, buffer);
	safe_unpackstr(&slurm_conf.propagate_rlimits_except, buffer);
	/* reboot_program */
	/* reconfig_flags */
	/* requeue_exit */
	/* requeue_exit_hold */
	/* resume_fail_program */
	/* resume_program */
	/* resume_rate */
	/* resume_timeout */
	/* resv_epilog */
	/* resv_over_run */
	/* resv_prolog */
	/* ret2service */
	/* route_plugin */
	/* sched_logfile */
	/* sched_log_level */
	/* sched_params */
	/* sched_time_slice */
	/* schedtype */
	/* scron_params */
	safe_unpackstr(&slurm_conf.select_type, buffer);
	/* select_conf_key_pairs */
	safe_unpack16(&slurm_conf.select_type_param, buffer);
	/* site_factor_plugin */
	/* site_factor_params */
	/* &slurm_conf */
	safe_unpack32(&slurm_conf.slurm_user_id, buffer);
	/* slurm_user_name */
	safe_unpack32(&slurm_conf.slurmd_user_id, buffer);
	/* slurmd_user_name */
	safe_unpackstr(&slurm_conf.slurmctld_addr, buffer);
	/* slurmctld_debug */
	/* slurmctld_logfile */
	/* slurmctld_pidfile */
	/* slurmctld_plugstack */
	/* slurmctld_plugstack_conf */
	safe_unpack32(&slurm_conf.slurmctld_port, buffer);
	safe_unpack16(&slurm_conf.slurmctld_port_count, buffer);
	/* slurmctld_primary_off_prog */
	/* slurmctld_primary_on_prog */
	/* slurmctld_syslog_debug */
	/* slurmctld_timeout */
	/* slurmctld_params */
	/* slurmd_debug */
	/* slurmd_logfile */
	/* slurmd_params */
	/* slurmd_pidfile */
	/* slurmd_port */
	safe_unpackstr(&slurm_conf.slurmd_spooldir, buffer);
	/* slurmd_syslog_debug */
	/* slurmd_timeout */
	/* srun_epilog */
	/* srun_port_range */
	/* srun_prolog */
	/* state_save_location */
	/* suspend_exc_nodes */
	/* suspend_exc_parts */
	/* suspend_program */
	/* suspend_rate */
	/* suspend_time */
	/* suspend_timeout */
	safe_unpackstr(&slurm_conf.switch_type, buffer);
	safe_unpackstr(&slurm_conf.switch_param, buffer);
	safe_unpackstr(&slurm_conf.task_epilog, buffer);
	safe_unpackstr(&slurm_conf.task_plugin, buffer);
	safe_unpack32(&slurm_conf.task_plugin_param, buffer);
	safe_unpackstr(&slurm_conf.task_prolog, buffer);
	safe_unpack16(&slurm_conf.tcp_timeout, buffer);
	safe_unpackstr(&slurm_conf.tmp_fs, buffer);
	/* topology_param */
	/* topology_plugin */
	safe_unpack16(&slurm_conf.tree_width, buffer);
	safe_unpackstr(&slurm_conf.unkillable_program, buffer);
	safe_unpack16(&slurm_conf.unkillable_timeout, buffer);
	/* version */
	safe_unpack16(&slurm_conf.vsize_factor, buffer);
	safe_unpack16(&slurm_conf.wait_time, buffer);
	safe_unpackstr(&slurm_conf.x11_params, buffer);

	return SLURM_SUCCESS;
unpack_error:
	error("unpack_error in %s: %m", __func__);

	free_slurm_conf(&slurm_conf, false);

	return SLURM_ERROR;
}
