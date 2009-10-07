/*
 * ctl_conf.c - convert data between slurm config and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurm.h>
#include "msg.h"


/*
 * convert slurm_ctl_conf_t into perl HV 
 */
int
slurm_ctl_conf_to_hv(slurm_ctl_conf_t* conf, HV* hv)
{
	STORE_FIELD(hv, conf, last_update, time_t);
	STORE_FIELD(hv, conf, accounting_storage_enforce, uint16_t);
	if(conf->accounting_storage_backup_host)
		STORE_FIELD(hv, conf, accounting_storage_backup_host, charp);
	if(conf->accounting_storage_host)
		STORE_FIELD(hv, conf, accounting_storage_host, charp);
	if(conf->accounting_storage_loc)
		STORE_FIELD(hv, conf, accounting_storage_loc, charp);
	if(conf->accounting_storage_pass)
		STORE_FIELD(hv, conf, accounting_storage_pass, charp);
	STORE_FIELD(hv, conf, accounting_storage_port, uint32_t);
	if(conf->accounting_storage_type)
		STORE_FIELD(hv, conf, accounting_storage_type, charp);
	if(conf->accounting_storage_user)
		STORE_FIELD(hv, conf, accounting_storage_user, charp);
	if(conf->authtype)
		STORE_FIELD(hv, conf, authtype, charp);
	if(conf->backup_addr)
		STORE_FIELD(hv, conf, backup_addr, charp);
	if(conf->backup_controller)
		STORE_FIELD(hv, conf, backup_controller, charp);
	STORE_FIELD(hv, conf, boot_time, time_t);
	STORE_FIELD(hv, conf, cache_groups, uint16_t);
	if(conf->checkpoint_type)
		STORE_FIELD(hv, conf, checkpoint_type, charp);
	if(conf->cluster_name)
		STORE_FIELD(hv, conf, cluster_name, charp);
	if(conf->control_addr)
		STORE_FIELD(hv, conf, control_addr, charp);
	if(conf->control_machine)
		STORE_FIELD(hv, conf, control_machine, charp);
	if(conf->crypto_type)
		STORE_FIELD(hv, conf, crypto_type, charp);
	STORE_FIELD(hv, conf, debug_flags, uint32_t);
	STORE_FIELD(hv, conf, def_mem_per_cpu, uint32_t);
	STORE_FIELD(hv, conf, disable_root_jobs, uint16_t);
	STORE_FIELD(hv, conf, enforce_part_limits, uint16_t);
	if(conf->epilog)
		STORE_FIELD(hv, conf, epilog, charp);
	STORE_FIELD(hv, conf, epilog_msg_time, uint32_t);
	if(conf->epilog_slurmctld)
		STORE_FIELD(hv, conf, epilog_slurmctld, charp);
	STORE_FIELD(hv, conf, fast_schedule, uint16_t);
	STORE_FIELD(hv, conf, first_job_id, uint32_t);
	STORE_FIELD(hv, conf, health_check_interval, uint16_t);
	if(conf->health_check_program)
		STORE_FIELD(hv, conf, health_check_program, charp);
	STORE_FIELD(hv, conf, inactive_limit, uint16_t);
	STORE_FIELD(hv, conf, job_acct_gather_freq, uint16_t);
	if(conf->job_acct_gather_type)
		STORE_FIELD(hv, conf, job_acct_gather_type, charp);
	if(conf->job_ckpt_dir)
		STORE_FIELD(hv, conf, job_ckpt_dir, charp);
	if(conf->job_comp_host)
		STORE_FIELD(hv, conf, job_comp_host, charp);
	if(conf->job_comp_loc)
		STORE_FIELD(hv, conf, job_comp_loc, charp);
	if(conf->job_comp_pass)
		STORE_FIELD(hv, conf, job_comp_pass, charp);
	STORE_FIELD(hv, conf, job_comp_port, uint32_t);
	if(conf->job_comp_type)
		STORE_FIELD(hv, conf, job_comp_type, charp);
	if(conf->job_comp_user)
		STORE_FIELD(hv, conf, job_comp_user, charp);
	if(conf->job_credential_private_key)
		STORE_FIELD(hv, conf, job_credential_private_key, charp);
	if(conf->job_credential_public_certificate)
		STORE_FIELD(hv, conf, job_credential_public_certificate, charp);
	STORE_FIELD(hv, conf, job_file_append, uint16_t); 
	STORE_FIELD(hv, conf, job_requeue, uint16_t); 
	STORE_FIELD(hv, conf, kill_on_bad_exit, uint16_t);
	STORE_FIELD(hv, conf, kill_wait, uint16_t);
	if(conf->licenses)
		STORE_FIELD(hv, conf, licenses, charp);
	if(conf->mail_prog)
		STORE_FIELD(hv, conf, mail_prog, charp);
	STORE_FIELD(hv, conf, max_job_cnt, uint16_t);
	STORE_FIELD(hv, conf, max_mem_per_cpu, uint32_t);
	STORE_FIELD(hv, conf, min_job_age, uint16_t);
	if(conf->mpi_default)
		STORE_FIELD(hv, conf, mpi_default, charp);
	if(conf->mpi_params)
		STORE_FIELD(hv, conf, mpi_params, charp);
	STORE_FIELD(hv, conf, msg_timeout, uint16_t);
	STORE_FIELD(hv, conf, next_job_id, uint32_t);
	if(conf->node_prefix)
		STORE_FIELD(hv, conf, node_prefix, charp);
	STORE_FIELD(hv, conf, over_time_limit, uint16_t);
	if(conf->plugindir)
		STORE_FIELD(hv, conf, plugindir, charp);
	if(conf->plugstack)
		STORE_FIELD(hv, conf, plugstack, charp);
	STORE_FIELD(hv, conf, priority_decay_hl, uint32_t);
	STORE_FIELD(hv, conf, priority_favor_small, uint16_t);
	STORE_FIELD(hv, conf, priority_max_age, uint32_t);
	STORE_FIELD(hv, conf, priority_reset_period, uint16_t);
	STORE_FIELD(hv, conf, priority_type, charp);
	STORE_FIELD(hv, conf, priority_weight_age, uint32_t);
	STORE_FIELD(hv, conf, priority_weight_fs, uint32_t);
	STORE_FIELD(hv, conf, priority_weight_js, uint32_t);
	STORE_FIELD(hv, conf, priority_weight_part, uint32_t);
	STORE_FIELD(hv, conf, priority_weight_qos, uint32_t);
	STORE_FIELD(hv, conf, private_data, uint16_t);
	if(conf->proctrack_type)
		STORE_FIELD(hv, conf, proctrack_type, charp);
	if(conf->prolog)
		STORE_FIELD(hv, conf, prolog, charp);
	if(conf->prolog_slurmctld)
		STORE_FIELD(hv, conf, prolog_slurmctld, charp);
	STORE_FIELD(hv, conf, propagate_prio_process, uint16_t);
	if(conf->propagate_rlimits)
		STORE_FIELD(hv, conf, propagate_rlimits, charp);
	if(conf->propagate_rlimits_except)
		STORE_FIELD(hv, conf, propagate_rlimits_except, charp);
	STORE_FIELD(hv, conf, resume_rate, uint16_t);
	if(conf->resume_program)
		STORE_FIELD(hv, conf, resume_program, charp);
	STORE_FIELD(hv, conf, resv_over_run, uint16_t);
	STORE_FIELD(hv, conf, ret2service, uint16_t);
	if(conf->salloc_default_command)
		STORE_FIELD(hv, conf, salloc_default_command, charp);
	if(conf->sched_params)
		STORE_FIELD(hv, conf, sched_params, charp);
	STORE_FIELD(hv, conf, sched_time_slice, uint16_t);
	if(conf->schedtype)
		STORE_FIELD(hv, conf, schedtype, charp);
	STORE_FIELD(hv, conf, schedport, uint16_t);
	STORE_FIELD(hv, conf, schedrootfltr, uint16_t);
	if(conf->select_type)
		STORE_FIELD(hv, conf, select_type, charp);
	STORE_FIELD(hv, conf, select_type_param, uint16_t);
	STORE_FIELD(hv, conf, slurm_user_id, uint32_t);
	if(conf->slurm_user_name)
		STORE_FIELD(hv, conf, slurm_user_name, charp);
	STORE_FIELD(hv, conf, slurmd_user_id, uint32_t);
	if(conf->slurmd_user_name)
		STORE_FIELD(hv, conf, slurmd_user_name, charp);
	STORE_FIELD(hv, conf, slurmctld_debug, uint16_t);
	if(conf->slurmctld_logfile)
		STORE_FIELD(hv, conf, slurmctld_logfile, charp);
	if(conf->slurmctld_pidfile)
		STORE_FIELD(hv, conf, slurmctld_pidfile, charp);
	STORE_FIELD(hv, conf, slurmctld_port, uint16_t);
	STORE_FIELD(hv, conf, slurmctld_timeout, uint16_t);
	STORE_FIELD(hv, conf, slurmd_debug, uint16_t);
	if(conf->slurmd_logfile)
		STORE_FIELD(hv, conf, slurmd_logfile, charp);
	if(conf->slurmd_pidfile)
		STORE_FIELD(hv, conf, slurmd_pidfile, charp);
	STORE_FIELD(hv, conf, slurmd_port, uint32_t);
	if(conf->slurmd_spooldir)
		STORE_FIELD(hv, conf, slurmd_spooldir, charp);
	STORE_FIELD(hv, conf, slurmd_timeout, uint16_t);
	if(conf->slurm_conf)
		STORE_FIELD(hv, conf, slurm_conf, charp);
	if(conf->srun_epilog)
		STORE_FIELD(hv, conf, srun_epilog, charp);
	if(conf->srun_prolog)
		STORE_FIELD(hv, conf, srun_prolog, charp);
	if(conf->state_save_location)
		STORE_FIELD(hv, conf, state_save_location, charp);
	if(conf->suspend_exc_nodes)
		STORE_FIELD(hv, conf, suspend_exc_nodes, charp);
	if(conf->suspend_exc_parts)
		STORE_FIELD(hv, conf, suspend_exc_parts, charp);
	if(conf->suspend_program)
		STORE_FIELD(hv, conf, suspend_program, charp);
	STORE_FIELD(hv, conf, suspend_rate, uint16_t);
	STORE_FIELD(hv, conf, suspend_time, uint16_t);
	if(conf->switch_type)
		STORE_FIELD(hv, conf, switch_type, charp);
	if(conf->task_epilog)
		STORE_FIELD(hv, conf, task_epilog, charp);
	if(conf->task_plugin)
		STORE_FIELD(hv, conf, task_plugin, charp);
	STORE_FIELD(hv, conf, task_plugin_param, uint16_t);
	if(conf->task_prolog)
		STORE_FIELD(hv, conf, task_prolog, charp);
	if(conf->tmp_fs)
		STORE_FIELD(hv, conf, tmp_fs, charp);
	if(conf->topology_plugin)
		STORE_FIELD(hv, conf, topology_plugin, charp);
	STORE_FIELD(hv, conf, track_wckey, uint16_t);
	STORE_FIELD(hv, conf, tree_width, uint16_t);
	if(conf->unkillable_program)
		STORE_FIELD(hv, conf, unkillable_program, charp);
	STORE_FIELD(hv, conf, unkillable_timeout, uint16_t);
	STORE_FIELD(hv, conf, use_pam, uint16_t);
	STORE_FIELD(hv, conf, wait_time, uint16_t);
	return 0;
}

