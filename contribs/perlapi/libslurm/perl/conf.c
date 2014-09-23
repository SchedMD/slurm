/*
 * conf.c - convert data between slurm config and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurm.h>
#include "slurm-perl.h"


/*
 * convert slurm_ctl_conf_t into perl HV
 */
int
slurm_ctl_conf_to_hv(slurm_ctl_conf_t *conf, HV *hv)
{
	STORE_FIELD(hv, conf, last_update, time_t);
	if (conf->acct_gather_profile_type)
		STORE_FIELD(hv, conf, acct_gather_profile_type, charp);
	if (conf->acct_gather_infiniband_type)
		STORE_FIELD(hv, conf, acct_gather_infiniband_type, charp);
	if (conf->acct_gather_filesystem_type)
		STORE_FIELD(hv, conf, acct_gather_filesystem_type, charp);
	STORE_FIELD(hv, conf, accounting_storage_enforce, uint16_t);
	if (conf->accounting_storage_backup_host)
		STORE_FIELD(hv, conf, accounting_storage_backup_host, charp);
	if (conf->accounting_storage_host)
		STORE_FIELD(hv, conf, accounting_storage_host, charp);
	if (conf->accounting_storage_loc)
		STORE_FIELD(hv, conf, accounting_storage_loc, charp);
	/* accounting_storage_pass */
	STORE_FIELD(hv, conf, accounting_storage_port, uint32_t);
	if (conf->accounting_storage_type)
		STORE_FIELD(hv, conf, accounting_storage_type, charp);
	if (conf->accounting_storage_user)
		STORE_FIELD(hv, conf, accounting_storage_user, charp);

	if (conf->authinfo)
		STORE_FIELD(hv, conf, authinfo, charp);
	if (conf->authtype)
		STORE_FIELD(hv, conf, authtype, charp);
	if (conf->backup_addr)
		STORE_FIELD(hv, conf, backup_addr, charp);
	if (conf->backup_controller)
		STORE_FIELD(hv, conf, backup_controller, charp);
	STORE_FIELD(hv, conf, batch_start_timeout, uint16_t);
	STORE_FIELD(hv, conf, boot_time, time_t);
	if (conf->checkpoint_type)
		STORE_FIELD(hv, conf, checkpoint_type, charp);
	if (conf->cluster_name)
		STORE_FIELD(hv, conf, cluster_name, charp);
	STORE_FIELD(hv, conf, complete_wait, uint16_t);

	if (conf->control_addr)
		STORE_FIELD(hv, conf, control_addr, charp);
	if (conf->control_machine)
		STORE_FIELD(hv, conf, control_machine, charp);
	if (conf->crypto_type)
		STORE_FIELD(hv, conf, crypto_type, charp);
	STORE_FIELD(hv, conf, debug_flags, uint64_t);
	STORE_FIELD(hv, conf, def_mem_per_cpu, uint32_t);
	STORE_FIELD(hv, conf, disable_root_jobs, uint16_t);
	STORE_FIELD(hv, conf, dynalloc_port, uint16_t);
	STORE_FIELD(hv, conf, enforce_part_limits, uint16_t);
	if (conf->epilog)
		STORE_FIELD(hv, conf, epilog, charp);
	STORE_FIELD(hv, conf, epilog_msg_time, uint32_t);
	if (conf->epilog_slurmctld)
		STORE_FIELD(hv, conf, epilog_slurmctld, charp);
	if (conf->ext_sensors_type)
		STORE_FIELD(hv, conf, ext_sensors_type, charp);
	STORE_FIELD(hv, conf, ext_sensors_freq, uint16_t);

	STORE_FIELD(hv, conf, fast_schedule, uint16_t);
	STORE_FIELD(hv, conf, first_job_id, uint32_t);
	STORE_FIELD(hv, conf, get_env_timeout, uint16_t);
	if (conf->gres_plugins)
		STORE_FIELD(hv, conf, gres_plugins, charp);
	STORE_FIELD(hv, conf, group_info, uint16_t);
	STORE_FIELD(hv, conf, hash_val, uint32_t);
	STORE_FIELD(hv, conf, health_check_interval, uint16_t);
	STORE_FIELD(hv, conf, health_check_node_state, uint32_t);
	if (conf->health_check_program)
		STORE_FIELD(hv, conf, health_check_program, charp);
	STORE_FIELD(hv, conf, inactive_limit, uint16_t);
	if (conf->job_acct_gather_type)
		STORE_FIELD(hv, conf, job_acct_gather_freq, charp);
	if (conf->job_acct_gather_type)
		STORE_FIELD(hv, conf, job_acct_gather_type, charp);

	if (conf->job_ckpt_dir)
		STORE_FIELD(hv, conf, job_ckpt_dir, charp);
	if (conf->job_comp_host)
		STORE_FIELD(hv, conf, job_comp_host, charp);
	if (conf->job_comp_loc)
		STORE_FIELD(hv, conf, job_comp_loc, charp);
	/* job_comp_pass */
	STORE_FIELD(hv, conf, job_comp_port, uint32_t);
	if (conf->job_comp_type)
		STORE_FIELD(hv, conf, job_comp_type, charp);
	if (conf->job_comp_user)
		STORE_FIELD(hv, conf, job_comp_user, charp);
	if (conf->job_credential_private_key)
		STORE_FIELD(hv, conf, job_credential_private_key, charp);
	if (conf->job_credential_public_certificate)
		STORE_FIELD(hv, conf, job_credential_public_certificate, charp);
	STORE_FIELD(hv, conf, job_file_append, uint16_t);
	STORE_FIELD(hv, conf, job_requeue, uint16_t);
	if (conf->job_submit_plugins)
		STORE_FIELD(hv, conf, job_submit_plugins, charp);

	STORE_FIELD(hv, conf, keep_alive_time, uint16_t);
	STORE_FIELD(hv, conf, kill_on_bad_exit, uint16_t);
	STORE_FIELD(hv, conf, kill_wait, uint16_t);
	if (conf->licenses)
		STORE_FIELD(hv, conf, licenses, charp);
	if (conf->mail_prog)
		STORE_FIELD(hv, conf, mail_prog, charp);
	STORE_FIELD(hv, conf, max_array_sz, uint16_t);
	STORE_FIELD(hv, conf, max_job_cnt, uint16_t);
	STORE_FIELD(hv, conf, max_mem_per_cpu, uint32_t);
	STORE_FIELD(hv, conf, max_tasks_per_node, uint16_t);
	STORE_FIELD(hv, conf, min_job_age, uint16_t);
	if (conf->mpi_default)
		STORE_FIELD(hv, conf, mpi_default, charp);
	if (conf->mpi_params)
		STORE_FIELD(hv, conf, mpi_params, charp);
	STORE_FIELD(hv, conf, msg_timeout, uint16_t);
	STORE_FIELD(hv, conf, next_job_id, uint32_t);

	if (conf->node_prefix)
		STORE_FIELD(hv, conf, node_prefix, charp);
	STORE_FIELD(hv, conf, over_time_limit, uint16_t);
	if (conf->plugindir)
		STORE_FIELD(hv, conf, plugindir, charp);
	if (conf->plugstack)
		STORE_FIELD(hv, conf, plugstack, charp);
	STORE_FIELD(hv, conf, preempt_mode, uint16_t);
	if (conf->preempt_type)
		STORE_FIELD(hv, conf, preempt_type, charp);
	STORE_FIELD(hv, conf, priority_calc_period, uint32_t);
	STORE_FIELD(hv, conf, priority_decay_hl, uint32_t);
	STORE_FIELD(hv, conf, priority_favor_small, uint16_t);
	STORE_FIELD(hv, conf, priority_max_age, uint32_t);
	if (conf->priority_params)
		STORE_FIELD(hv, conf, priority_params, charp);
	STORE_FIELD(hv, conf, priority_reset_period, uint16_t);
	if (conf->priority_type)
		STORE_FIELD(hv, conf, priority_type, charp);
	STORE_FIELD(hv, conf, prolog_flags, uint16_t);

	STORE_FIELD(hv, conf, priority_weight_age, uint32_t);
	STORE_FIELD(hv, conf, priority_weight_fs, uint32_t);
	STORE_FIELD(hv, conf, priority_weight_js, uint32_t);
	STORE_FIELD(hv, conf, priority_weight_part, uint32_t);
	STORE_FIELD(hv, conf, priority_weight_qos, uint32_t);
	STORE_FIELD(hv, conf, private_data, uint16_t);
	if (conf->proctrack_type)
		STORE_FIELD(hv, conf, proctrack_type, charp);
	if (conf->prolog)
		STORE_FIELD(hv, conf, prolog, charp);
	if (conf->prolog_slurmctld)
		STORE_FIELD(hv, conf, prolog_slurmctld, charp);

	STORE_FIELD(hv, conf, propagate_prio_process, uint16_t);
	if (conf->propagate_rlimits)
		STORE_FIELD(hv, conf, propagate_rlimits, charp);
	if (conf->propagate_rlimits_except)
		STORE_FIELD(hv, conf, propagate_rlimits_except, charp);
	STORE_FIELD(hv, conf, reconfig_flags, uint16_t);
	if (conf->resume_program)
		STORE_FIELD(hv, conf, resume_program, charp);
	STORE_FIELD(hv, conf, resume_rate, uint16_t);
	STORE_FIELD(hv, conf, resume_timeout, uint16_t);
	if (conf->resv_epilog)
		STORE_FIELD(hv, conf, resv_epilog, charp);
	STORE_FIELD(hv, conf, resv_over_run, uint16_t);
	if (conf->resv_prolog)
		STORE_FIELD(hv, conf, resv_prolog, charp);
	STORE_FIELD(hv, conf, ret2service, uint16_t);
	if (conf->salloc_default_command)
		STORE_FIELD(hv, conf, salloc_default_command, charp);

	if (conf->sched_logfile)
		STORE_FIELD(hv, conf, sched_logfile, charp);
	STORE_FIELD(hv, conf, sched_log_level, uint16_t);
	if (conf->sched_params)
		STORE_FIELD(hv, conf, sched_params, charp);
	STORE_FIELD(hv, conf, sched_time_slice, uint16_t);
	if (conf->schedtype)
		STORE_FIELD(hv, conf, schedtype, charp);
	STORE_FIELD(hv, conf, schedport, uint16_t);
	STORE_FIELD(hv, conf, schedrootfltr, uint16_t);
	if (conf->select_type)
		STORE_FIELD(hv, conf, select_type, charp);
	STORE_PTR_FIELD(hv, conf, select_conf_key_pairs, "Slurm::List"); /* TODO: Think about memory management */
	STORE_FIELD(hv, conf, select_type_param, uint16_t);
	if (conf->slurm_conf)
		STORE_FIELD(hv, conf, slurm_conf, charp);

	STORE_FIELD(hv, conf, slurm_user_id, uint32_t);
	if (conf->slurm_user_name)
		STORE_FIELD(hv, conf, slurm_user_name, charp);
	STORE_FIELD(hv, conf, slurmd_user_id, uint32_t);
	if (conf->slurmd_user_name)
		STORE_FIELD(hv, conf, slurmd_user_name, charp);
	STORE_FIELD(hv, conf, slurmctld_debug, uint16_t);
	if (conf->slurmctld_logfile)
		STORE_FIELD(hv, conf, slurmctld_logfile, charp);
	if (conf->slurmctld_pidfile)
		STORE_FIELD(hv, conf, slurmctld_pidfile, charp);
	if (conf->slurmctld_plugstack)
		STORE_FIELD(hv, conf, slurmctld_plugstack, charp);
	STORE_FIELD(hv, conf, slurmctld_port, uint32_t);
	STORE_FIELD(hv, conf, slurmctld_port_count, uint16_t);
	STORE_FIELD(hv, conf, slurmctld_timeout, uint16_t);

	STORE_FIELD(hv, conf, slurmd_debug, uint16_t);
	if (conf->slurmd_logfile)
		STORE_FIELD(hv, conf, slurmd_logfile, charp);
	if (conf->slurmd_pidfile)
		STORE_FIELD(hv, conf, slurmd_pidfile, charp);
	STORE_FIELD(hv, conf, slurmd_port, uint32_t);
	if (conf->slurmd_spooldir)
		STORE_FIELD(hv, conf, slurmd_spooldir, charp);
	STORE_FIELD(hv, conf, slurmd_timeout, uint16_t);
	if (conf->srun_epilog)
		STORE_FIELD(hv, conf, srun_epilog, charp);
	if (conf->srun_prolog)
		STORE_FIELD(hv, conf, srun_prolog, charp);
	if (conf->state_save_location)
		STORE_FIELD(hv, conf, state_save_location, charp);
	if (conf->suspend_exc_nodes)
		STORE_FIELD(hv, conf, suspend_exc_nodes, charp);
	if (conf->suspend_exc_parts)
		STORE_FIELD(hv, conf, suspend_exc_parts, charp);
	if (conf->suspend_program)
		STORE_FIELD(hv, conf, suspend_program, charp);
	STORE_FIELD(hv, conf, suspend_rate, uint16_t);
	STORE_FIELD(hv, conf, suspend_time, uint32_t);
	STORE_FIELD(hv, conf, suspend_timeout, uint16_t);

	if (conf->switch_type)
		STORE_FIELD(hv, conf, switch_type, charp);
	if (conf->task_epilog)
		STORE_FIELD(hv, conf, task_epilog, charp);
	if (conf->task_plugin)
		STORE_FIELD(hv, conf, task_plugin, charp);
	STORE_FIELD(hv, conf, task_plugin_param, uint16_t);
	if (conf->task_prolog)
		STORE_FIELD(hv, conf, task_prolog, charp);
	if (conf->tmp_fs)
		STORE_FIELD(hv, conf, tmp_fs, charp);
	if (conf->topology_plugin)
		STORE_FIELD(hv, conf, topology_plugin, charp);
	STORE_FIELD(hv, conf, track_wckey, uint16_t);
	STORE_FIELD(hv, conf, tree_width, uint16_t);
	if (conf->unkillable_program)
		STORE_FIELD(hv, conf, unkillable_program, charp);
	STORE_FIELD(hv, conf, unkillable_timeout, uint16_t);
	STORE_FIELD(hv, conf, use_pam, uint16_t);
	STORE_FIELD(hv, conf, use_spec_resources, uint16_t);
	if (conf->version)
		STORE_FIELD(hv, conf, version, charp);
	STORE_FIELD(hv, conf, vsize_factor, uint16_t);
	STORE_FIELD(hv, conf, wait_time, uint16_t);
	return 0;
}

/*
 * convert perl HV to slurm_ctl_conf_t
 */
int
hv_to_slurm_ctl_conf(HV *hv, slurm_ctl_conf_t *conf)
{
	memset(conf, 0, sizeof(slurm_ctl_conf_t));

	FETCH_FIELD(hv, conf, last_update, time_t, TRUE);
	FETCH_FIELD(hv, conf, acct_gather_profile_type, charp, FALSE);
	FETCH_FIELD(hv, conf, acct_gather_infiniband_type, charp, FALSE);
	FETCH_FIELD(hv, conf, acct_gather_filesystem_type, charp, FALSE);
	FETCH_FIELD(hv, conf, accounting_storage_enforce, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, accounting_storage_backup_host, charp, FALSE);
	FETCH_FIELD(hv, conf, accounting_storage_host, charp, FALSE);
	FETCH_FIELD(hv, conf, accounting_storage_loc, charp, FALSE);
	FETCH_FIELD(hv, conf, accounting_storage_port, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, accounting_storage_type, charp, FALSE);
	FETCH_FIELD(hv, conf, accounting_storage_user, charp, FALSE);

	FETCH_FIELD(hv, conf, authinfo, charp, FALSE);
	FETCH_FIELD(hv, conf, authtype, charp, FALSE);
	FETCH_FIELD(hv, conf, backup_addr, charp, FALSE);
	FETCH_FIELD(hv, conf, backup_controller, charp, FALSE);
	FETCH_FIELD(hv, conf, batch_start_timeout, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, boot_time, time_t, TRUE);
	FETCH_FIELD(hv, conf, checkpoint_type, charp, FALSE);
	FETCH_FIELD(hv, conf, cluster_name, charp, FALSE);
	FETCH_FIELD(hv, conf, complete_wait, uint16_t, TRUE);

	FETCH_FIELD(hv, conf, control_addr, charp, FALSE);
	FETCH_FIELD(hv, conf, control_machine, charp, FALSE);
	FETCH_FIELD(hv, conf, crypto_type, charp, FALSE);
	FETCH_FIELD(hv, conf, debug_flags, uint64_t, TRUE);
	FETCH_FIELD(hv, conf, def_mem_per_cpu, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, disable_root_jobs, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, dynalloc_port, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, enforce_part_limits, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, epilog, charp, FALSE);
	FETCH_FIELD(hv, conf, epilog_msg_time, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, epilog_slurmctld, charp, FALSE);
	FETCH_FIELD(hv, conf, ext_sensors_freq, uint16_t, TRUE);

	FETCH_FIELD(hv, conf, fast_schedule, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, first_job_id, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, get_env_timeout, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, gres_plugins, charp, FALSE);
	FETCH_FIELD(hv, conf, group_info, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, hash_val, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, health_check_interval, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, health_check_node_state, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, health_check_program, charp, FALSE);
	FETCH_FIELD(hv, conf, inactive_limit, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, job_acct_gather_freq, charp, TRUE);
	FETCH_FIELD(hv, conf, job_acct_gather_type, charp, FALSE);

	FETCH_FIELD(hv, conf, job_ckpt_dir, charp, FALSE);
	FETCH_FIELD(hv, conf, job_comp_host, charp, FALSE);
	FETCH_FIELD(hv, conf, job_comp_loc, charp, FALSE);
	FETCH_FIELD(hv, conf, job_comp_port, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, job_comp_type, charp, FALSE);
	FETCH_FIELD(hv, conf, job_comp_user, charp, FALSE);
	FETCH_FIELD(hv, conf, job_credential_private_key, charp, FALSE);
	FETCH_FIELD(hv, conf, job_credential_public_certificate, charp, FALSE);
	FETCH_FIELD(hv, conf, job_file_append, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, job_requeue, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, job_submit_plugins, charp, FALSE);

	FETCH_FIELD(hv, conf, keep_alive_time, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, kill_on_bad_exit, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, kill_wait, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, licenses, charp, FALSE);
	FETCH_FIELD(hv, conf, mail_prog, charp, FALSE);
	FETCH_FIELD(hv, conf, max_array_sz, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, max_job_cnt, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, max_mem_per_cpu, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, max_tasks_per_node, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, min_job_age, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, mpi_default, charp, FALSE);
	FETCH_FIELD(hv, conf, mpi_params, charp, FALSE);
	FETCH_FIELD(hv, conf, msg_timeout, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, next_job_id, uint32_t, TRUE);

	FETCH_FIELD(hv, conf, node_prefix, charp, FALSE);
	FETCH_FIELD(hv, conf, over_time_limit, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, plugindir, charp, FALSE);
	FETCH_FIELD(hv, conf, plugstack, charp, FALSE);
	FETCH_FIELD(hv, conf, preempt_mode, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, preempt_type, charp, FALSE);
	FETCH_FIELD(hv, conf, priority_calc_period, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, priority_decay_hl, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, priority_favor_small, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, priority_max_age, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, priority_params, charp, FALSE);
	FETCH_FIELD(hv, conf, priority_reset_period, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, priority_type, charp, FALSE);

	FETCH_FIELD(hv, conf, priority_weight_age, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, priority_weight_fs, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, priority_weight_js, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, priority_weight_part, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, priority_weight_qos, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, private_data, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, proctrack_type, charp, FALSE);
	FETCH_FIELD(hv, conf, prolog, charp, FALSE);
	FETCH_FIELD(hv, conf, prolog_slurmctld, charp, FALSE);
	FETCH_FIELD(hv, conf, prolog_flags, uint16_t, TRUE);

	FETCH_FIELD(hv, conf, propagate_prio_process, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, propagate_rlimits, charp, FALSE);
	FETCH_FIELD(hv, conf, propagate_rlimits_except, charp, FALSE);
	FETCH_FIELD(hv, conf, reconfig_flags, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, resume_program, charp, FALSE);
	FETCH_FIELD(hv, conf, resume_rate, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, resume_timeout, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, resv_epilog, charp, FALSE);
	FETCH_FIELD(hv, conf, resv_over_run, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, resv_prolog, charp, FALSE);
	FETCH_FIELD(hv, conf, ret2service, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, salloc_default_command, charp, FALSE);

	FETCH_FIELD(hv, conf, sched_logfile, charp, FALSE);
	FETCH_FIELD(hv, conf, sched_log_level, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, sched_params, charp, FALSE);
	FETCH_FIELD(hv, conf, sched_time_slice, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, schedtype, charp, FALSE);
	FETCH_FIELD(hv, conf, schedport, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, schedrootfltr, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, select_type, charp, FALSE);
	/* TODO: select_conf_key_pairs */
	FETCH_FIELD(hv, conf, select_type_param, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, slurm_conf, charp, FALSE);

	FETCH_FIELD(hv, conf, slurm_user_id, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, slurm_user_name, charp, FALSE);
	FETCH_FIELD(hv, conf, slurmd_user_id, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, slurmd_user_name, charp, FALSE);
	FETCH_FIELD(hv, conf, slurmctld_debug, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, slurmctld_logfile, charp, FALSE);
	FETCH_FIELD(hv, conf, slurmctld_pidfile, charp, FALSE);
	FETCH_FIELD(hv, conf, slurmctld_plugstack, charp, FALSE);
	FETCH_FIELD(hv, conf, slurmctld_port, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, slurmctld_port_count, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, slurmctld_timeout, uint16_t, TRUE);

	FETCH_FIELD(hv, conf, slurmd_debug, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, slurmd_logfile, charp, FALSE);
	FETCH_FIELD(hv, conf, slurmd_pidfile, charp, FALSE);
	FETCH_FIELD(hv, conf, slurmd_port, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, slurmd_spooldir, charp, FALSE);
	FETCH_FIELD(hv, conf, slurmd_timeout, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, srun_epilog, charp, FALSE);
	FETCH_FIELD(hv, conf, srun_prolog, charp, FALSE);
	FETCH_FIELD(hv, conf, state_save_location, charp, FALSE);
	FETCH_FIELD(hv, conf, suspend_exc_nodes, charp, FALSE);
	FETCH_FIELD(hv, conf, suspend_exc_parts, charp, FALSE);
	FETCH_FIELD(hv, conf, suspend_program, charp, FALSE);
	FETCH_FIELD(hv, conf, suspend_rate, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, suspend_time, uint32_t, TRUE);
	FETCH_FIELD(hv, conf, suspend_timeout, uint16_t, TRUE);

	FETCH_FIELD(hv, conf, switch_type, charp, FALSE);
	FETCH_FIELD(hv, conf, task_epilog, charp, FALSE);
	FETCH_FIELD(hv, conf, task_plugin, charp, FALSE);
	FETCH_FIELD(hv, conf, task_plugin_param, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, task_prolog, charp, FALSE);
	FETCH_FIELD(hv, conf, tmp_fs, charp, FALSE);
	FETCH_FIELD(hv, conf, topology_plugin, charp, FALSE);
	FETCH_FIELD(hv, conf, track_wckey, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, tree_width, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, unkillable_program, charp, FALSE);
	FETCH_FIELD(hv, conf, unkillable_timeout, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, use_pam, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, use_spec_resources, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, version, charp, FALSE);
	FETCH_FIELD(hv, conf, vsize_factor, uint16_t, TRUE);
	FETCH_FIELD(hv, conf, wait_time, uint16_t, TRUE);
	return 0;
}

/*
 * convert slurmd_status_t to perl HV
 */
int
slurmd_status_to_hv(slurmd_status_t *status, HV *hv)
{
	STORE_FIELD(hv, status, booted, time_t);
	STORE_FIELD(hv, status, last_slurmctld_msg, time_t);
	STORE_FIELD(hv, status, slurmd_debug, uint16_t);
	STORE_FIELD(hv, status, actual_cpus, uint16_t);
	STORE_FIELD(hv, status, actual_sockets, uint16_t);
	STORE_FIELD(hv, status, actual_cores, uint16_t);
	STORE_FIELD(hv, status, actual_threads, uint16_t);
	STORE_FIELD(hv, status, actual_real_mem, uint32_t);
	STORE_FIELD(hv, status, actual_tmp_disk, uint32_t);
	STORE_FIELD(hv, status, pid, uint32_t);
	if (status->hostname)
		STORE_FIELD(hv, status, hostname, charp);
	if (status->slurmd_logfile)
		STORE_FIELD(hv, status, slurmd_logfile, charp);
	if (status->step_list)
		STORE_FIELD(hv, status, step_list, charp);
	if (status->version)
		STORE_FIELD(hv, status, version, charp);

	return 0;
}

/*
 * convert perl HV to slurmd_status_t
 */
int
hv_to_slurmd_status(HV *hv, slurmd_status_t *status)
{
	memset(status, 0, sizeof(slurmd_status_t));

	FETCH_FIELD(hv, status, booted, time_t, TRUE);
	FETCH_FIELD(hv, status, last_slurmctld_msg, time_t, TRUE);
	FETCH_FIELD(hv, status, slurmd_debug, uint16_t, TRUE);
	FETCH_FIELD(hv, status, actual_cpus, uint16_t, TRUE);
	FETCH_FIELD(hv, status, actual_sockets, uint16_t, TRUE);
	FETCH_FIELD(hv, status, actual_cores, uint16_t, TRUE);
	FETCH_FIELD(hv, status, actual_threads, uint16_t, TRUE);
	FETCH_FIELD(hv, status, actual_real_mem, uint32_t, TRUE);
	FETCH_FIELD(hv, status, actual_tmp_disk, uint32_t, TRUE);
	FETCH_FIELD(hv, status, pid, uint32_t, TRUE);
	FETCH_FIELD(hv, status, hostname, charp, FALSE);
	FETCH_FIELD(hv, status, slurmd_logfile, charp, FALSE);
	FETCH_FIELD(hv, status, step_list, charp, FALSE);
	FETCH_FIELD(hv, status, version, charp, FALSE);

	return 0;
}

/*
 * convert perl HV to step_update_request_msg_t
 */
int
hv_to_step_update_request_msg(HV *hv, step_update_request_msg_t *update_msg)
{
	slurm_init_update_step_msg(update_msg);

	FETCH_FIELD(hv, update_msg, end_time, time_t, TRUE);
	FETCH_FIELD(hv, update_msg, exit_code, uint32_t, TRUE);
	FETCH_FIELD(hv, update_msg, job_id, uint32_t, TRUE);
	FETCH_FIELD(hv, update_msg, name, charp, FALSE);
	FETCH_FIELD(hv, update_msg, start_time, time_t, TRUE);
	FETCH_FIELD(hv, update_msg, step_id, uint32_t, TRUE);
	FETCH_FIELD(hv, update_msg, time_limit, uint32_t, TRUE);

	return 0;
}
