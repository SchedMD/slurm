/*****************************************************************************\
 *  proc_req.c - process incoming messages to slurmctld
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
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

#include "config.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/cron.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/group_cache.h"
#include "src/common/hostlist.h"
#include "src/common/id_util.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/node_features.h"
#include "src/common/pack.h"
#include "src/common/persist_conn.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_protocol_socket.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/acct_gather.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/certmgr.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/conn.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/jobcomp.h"
#include "src/interfaces/mcs.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/preempt.h"
#include "src/interfaces/priority.h"
#include "src/interfaces/sched_plugin.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/rpc_queue.h"
#include "src/slurmctld/sackd_mgr.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/statistics.h"
#include "src/slurmctld/trigger_mgr.h"

#include "src/stepmgr/srun_comm.h"
#include "src/stepmgr/stepmgr.h"

static pthread_mutex_t rpc_mutex = PTHREAD_MUTEX_INITIALIZER;
#define RPC_TYPE_SIZE 100
static uint16_t rpc_type_id[RPC_TYPE_SIZE] = { 0 };
static uint32_t rpc_type_cnt[RPC_TYPE_SIZE] = { 0 };
static uint64_t rpc_type_time[RPC_TYPE_SIZE] = { 0 };
static uint16_t rpc_type_queued[RPC_TYPE_SIZE] = { 0 };
static uint64_t rpc_type_dropped[RPC_TYPE_SIZE] = { 0 };
static uint16_t rpc_type_cycle_last[RPC_TYPE_SIZE] = { 0 };
static uint16_t rpc_type_cycle_max[RPC_TYPE_SIZE] = { 0 };
#define RPC_USER_SIZE 200
static uint32_t rpc_user_id[RPC_USER_SIZE] = { 0 };
static uint32_t rpc_user_cnt[RPC_USER_SIZE] = { 0 };
static uint64_t rpc_user_time[RPC_USER_SIZE] = { 0 };

static bool do_post_rpc_node_registration = false;

bool running_configless = false;
static pthread_rwlock_t configless_lock = PTHREAD_RWLOCK_INITIALIZER;
static config_response_msg_t *config_for_slurmd = NULL;
static config_response_msg_t *config_for_clients = NULL;

static pthread_mutex_t throttle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t throttle_cond = PTHREAD_COND_INITIALIZER;

static void         _create_het_job_id_set(hostset_t *jobid_hostset,
					    uint32_t het_job_offset,
					    char **het_job_id_set);
static void         _fill_ctld_conf(slurm_conf_t * build_ptr);
static void         _kill_job_on_msg_fail(uint32_t job_id);
static int _is_prolog_finished(slurm_step_id_t *step_id);
static int          _route_msg_to_origin(slurm_msg_t *msg, char *job_id_str,
					 uint32_t job_id);
static void         _throttle_fini(int *active_rpc_cnt);
static void         _throttle_start(int *active_rpc_cnt);

extern diag_stats_t slurmctld_diag_stats;

typedef struct {
	uid_t request_uid;
	uid_t uid;
	const char *id;
	list_t *step_list;
} find_job_by_container_id_args_t;

typedef struct {
	list_t *full_resp_list;
	slurm_msg_t *msg;
} foreach_multi_msg_t;

extern void record_rpc_stats(slurm_msg_t *msg, long delta)
{
	slurm_mutex_lock(&rpc_mutex);
	for (int i = 0; i < RPC_TYPE_SIZE; i++) {
		if (rpc_type_id[i] == 0)
			rpc_type_id[i] = msg->msg_type;
		else if (rpc_type_id[i] != msg->msg_type)
			continue;
		rpc_type_cnt[i]++;
		rpc_type_time[i] += delta;
		break;
	}
	for (int i = 0; i < RPC_USER_SIZE; i++) {
		if ((rpc_user_id[i] == 0) && (i != 0))
			rpc_user_id[i] = msg->auth_uid;
		else if (rpc_user_id[i] != msg->auth_uid)
			continue;
		rpc_user_cnt[i]++;
		rpc_user_time[i] += delta;
		break;
	}
	slurm_mutex_unlock(&rpc_mutex);
}

extern void record_rpc_queue_stats(slurmctld_rpc_t *q)
{
	slurm_mutex_lock(&rpc_mutex);
	for (int i = 0; i < RPC_TYPE_SIZE; i++) {
		if (!rpc_type_id[i])
			rpc_type_id[i] = q->msg_type;
		else if (rpc_type_id[i] != q->msg_type)
			continue;

		rpc_type_queued[i] = q->queued;
		rpc_type_dropped[i] = q->dropped;
		rpc_type_cycle_last[i] = q->cycle_last;
		rpc_type_cycle_max[i] = q->cycle_max;
		break;
	}
	slurm_mutex_unlock(&rpc_mutex);
}

/* These functions prevent certain RPCs from keeping the slurmctld write locks
 * constantly set, which can prevent other RPCs and system functions from being
 * processed. For example, a steady stream of batch submissions can prevent
 * squeue from responding or jobs from being scheduled. */
static void _throttle_start(int *active_rpc_cnt)
{
	slurm_mutex_lock(&throttle_mutex);
	while (1) {
		if (*active_rpc_cnt == 0) {
			(*active_rpc_cnt)++;
			break;
		}
#if 1
		slurm_cond_wait(&throttle_cond, &throttle_mutex);
#else
		/* While an RPC is being throttled due to a running RPC of the
		 * same type, do not count that thread against the daemon's
		 * thread limit. In extreme environments, this logic can result
		 * in the slurmctld spawning so many pthreads that it exhausts
		 * system resources and fails. */
		server_thread_decr();
		slurm_cond_wait(&throttle_cond, &throttle_mutex);
		server_thread_incr();
#endif
	}
	slurm_mutex_unlock(&throttle_mutex);
	if (LOTS_OF_AGENTS)
		usleep(1000);
	else
		usleep(1);
}
static void _throttle_fini(int *active_rpc_cnt)
{
	slurm_mutex_lock(&throttle_mutex);
	(*active_rpc_cnt)--;
	slurm_cond_broadcast(&throttle_cond);
	slurm_mutex_unlock(&throttle_mutex);
}

/*
 * _fill_ctld_conf - make a copy of current slurm configuration
 *	this is done with locks set so the data can change at other times
 * OUT conf_ptr - place to copy configuration to
 */
static void _fill_ctld_conf(slurm_conf_t *conf_ptr)
{
	slurm_conf_t *conf = &slurm_conf;
	uint32_t next_job_id;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	next_job_id   = get_next_job_id(true);

	memset(conf_ptr, 0, sizeof(*conf_ptr));

	conf_ptr->last_update         = time(NULL);
	conf_ptr->accounting_storage_enforce =
		conf->accounting_storage_enforce;
	conf_ptr->accounting_storage_host =
		xstrdup(conf->accounting_storage_host);
	conf_ptr->accounting_storage_ext_host =
		xstrdup(conf->accounting_storage_ext_host);
	conf_ptr->accounting_storage_backup_host =
		xstrdup(conf->accounting_storage_backup_host);
	conf_ptr->accounting_storage_params =
		xstrdup(conf->accounting_storage_params);
	conf_ptr->accounting_storage_port = conf->accounting_storage_port;
	conf_ptr->accounting_storage_tres =
		xstrdup(conf->accounting_storage_tres);
	conf_ptr->accounting_storage_type =
		xstrdup(conf->accounting_storage_type);

	conf_ptr->acct_gather_conf = acct_gather_conf_values();
	conf_ptr->acct_gather_energy_type =
		xstrdup(conf->acct_gather_energy_type);
	conf_ptr->acct_gather_filesystem_type =
		xstrdup(conf->acct_gather_filesystem_type);
	conf_ptr->acct_gather_interconnect_type =
		xstrdup(conf->acct_gather_interconnect_type);
	conf_ptr->acct_gather_profile_type =
		xstrdup(conf->acct_gather_profile_type);
	conf_ptr->acct_gather_node_freq = conf->acct_gather_node_freq;

	conf_ptr->authinfo            = xstrdup(conf->authinfo);
	conf_ptr->authtype            = xstrdup(conf->authtype);
	conf_ptr->authalttypes        = xstrdup(conf->authalttypes);
	conf_ptr->authalt_params      = xstrdup(conf->authalt_params);

	conf_ptr->batch_start_timeout = conf->batch_start_timeout;
	conf_ptr->boot_time           = slurmctld_config.boot_time;
	conf_ptr->bb_type             = xstrdup(conf->bb_type);
	conf_ptr->bcast_exclude       = xstrdup(conf->bcast_exclude);
	conf_ptr->bcast_parameters = xstrdup(conf->bcast_parameters);
	conf_ptr->certmgr_params = xstrdup(conf->certmgr_params);
	conf_ptr->certmgr_type = xstrdup(conf->certmgr_type);


	if (xstrstr(conf->job_acct_gather_type, "cgroup") ||
	    xstrstr(conf->proctrack_type, "cgroup") ||
	    xstrstr(conf->task_plugin, "cgroup"))
		conf_ptr->cgroup_conf = cgroup_get_conf_list();

	conf_ptr->cli_filter_params = xstrdup(conf->cli_filter_params);
	conf_ptr->cli_filter_plugins  = xstrdup(conf->cli_filter_plugins);
	conf_ptr->cluster_name        = xstrdup(conf->cluster_name);
	conf_ptr->comm_params         = xstrdup(conf->comm_params);
	conf_ptr->complete_wait       = conf->complete_wait;
	conf_ptr->conf_flags          = conf->conf_flags;
	conf_ptr->control_cnt         = conf->control_cnt;
	conf_ptr->control_addr = xcalloc(conf->control_cnt + 1, sizeof(char *));
	conf_ptr->control_machine = xcalloc(conf->control_cnt + 1,
					    sizeof(char *));
	for (int i = 0; i < conf_ptr->control_cnt; i++) {
		conf_ptr->control_addr[i] = xstrdup(conf->control_addr[i]);
		conf_ptr->control_machine[i] =
			xstrdup(conf->control_machine[i]);
	}
	conf_ptr->cpu_freq_def        = conf->cpu_freq_def;
	conf_ptr->cpu_freq_govs       = conf->cpu_freq_govs;
	conf_ptr->cred_type           = xstrdup(conf->cred_type);
	conf_ptr->data_parser_parameters =
		xstrdup(conf->data_parser_parameters);

	conf_ptr->def_mem_per_cpu     = conf->def_mem_per_cpu;
	conf_ptr->debug_flags         = conf->debug_flags;
	conf_ptr->dependency_params = xstrdup(conf->dependency_params);

	conf_ptr->eio_timeout         = conf->eio_timeout;
	conf_ptr->enforce_part_limits = conf->enforce_part_limits;
	conf_ptr->epilog_cnt = conf->epilog_cnt;
	conf_ptr->epilog = xcalloc(conf->epilog_cnt, sizeof(char *));
	for (int i = 0; i < conf_ptr->epilog_cnt; i++)
		conf_ptr->epilog[i] = xstrdup(conf->epilog[i]);
	conf_ptr->epilog_msg_time     = conf->epilog_msg_time;
	conf_ptr->epilog_slurmctld_cnt = conf->epilog_slurmctld_cnt;
	conf_ptr->epilog_slurmctld = xcalloc(conf->epilog_slurmctld_cnt,
					     sizeof(char *));
	for (int i = 0; i < conf_ptr->epilog_slurmctld_cnt; i++)
		conf_ptr->epilog_slurmctld[i] =
			xstrdup(conf->epilog_slurmctld[i]);
	conf_ptr->epilog_timeout = conf->epilog_timeout;
	conf_ptr->fed_params          = xstrdup(conf->fed_params);
	conf_ptr->first_job_id        = conf->first_job_id;
	conf_ptr->fs_dampening_factor = conf->fs_dampening_factor;

	conf_ptr->gres_plugins        = xstrdup(conf->gres_plugins);
	conf_ptr->group_time          = conf->group_time;
	conf_ptr->group_force         = conf->group_force;
	conf_ptr->gpu_freq_def        = xstrdup(conf->gpu_freq_def);

	conf_ptr->inactive_limit      = conf->inactive_limit;
	conf_ptr->interactive_step_opts = xstrdup(conf->interactive_step_opts);

	conf_ptr->hash_plugin = xstrdup(conf->hash_plugin);
	conf_ptr->hash_val            = conf->hash_val;
	conf_ptr->health_check_interval = conf->health_check_interval;
	conf_ptr->health_check_node_state = conf->health_check_node_state;
	conf_ptr->health_check_program = xstrdup(conf->health_check_program);
	conf_ptr->http_parser_type = xstrdup(conf->http_parser_type);

	conf_ptr->job_acct_gather_freq  = xstrdup(conf->job_acct_gather_freq);
	conf_ptr->job_acct_gather_type  = xstrdup(conf->job_acct_gather_type);
	conf_ptr->job_acct_gather_params= xstrdup(conf->job_acct_gather_params);
	conf_ptr->job_acct_oom_kill    = conf->job_acct_oom_kill;

	conf_ptr->job_comp_host       = xstrdup(conf->job_comp_host);
	conf_ptr->job_comp_loc        = xstrdup(conf->job_comp_loc);
	conf_ptr->job_comp_params = xstrdup(conf->job_comp_params);
	conf_ptr->job_comp_port       = conf->job_comp_port;
	conf_ptr->job_comp_type       = xstrdup(conf->job_comp_type);
	conf_ptr->job_comp_user       = xstrdup(conf->job_comp_user);
	conf_ptr->namespace_plugin = xstrdup(conf->namespace_plugin);

	conf_ptr->job_defaults_list   =
		job_defaults_copy(conf->job_defaults_list);
	conf_ptr->job_file_append     = conf->job_file_append;
	conf_ptr->job_requeue         = conf->job_requeue;
	conf_ptr->job_submit_plugins  = xstrdup(conf->job_submit_plugins);

	conf_ptr->keepalive_time = conf->keepalive_time;
	conf_ptr->kill_wait           = conf->kill_wait;
	conf_ptr->kill_on_bad_exit    = conf->kill_on_bad_exit;

	conf_ptr->launch_params       = xstrdup(conf->launch_params);
	conf_ptr->licenses            = xstrdup(conf->licenses);
	conf_ptr->log_fmt             = conf->log_fmt;

	conf_ptr->mail_domain         = xstrdup(conf->mail_domain);
	conf_ptr->mail_prog           = xstrdup(conf->mail_prog);
	conf_ptr->max_array_sz        = conf->max_array_sz;
	conf_ptr->max_batch_requeue   = conf->max_batch_requeue;
	conf_ptr->max_dbd_msgs        = conf->max_dbd_msgs;
	conf_ptr->max_job_cnt         = conf->max_job_cnt;
	conf_ptr->max_job_id          = conf->max_job_id;
	conf_ptr->max_mem_per_cpu     = conf->max_mem_per_cpu;
	conf_ptr->max_node_cnt        = conf->max_node_cnt;
	conf_ptr->max_step_cnt        = conf->max_step_cnt;
	conf_ptr->max_tasks_per_node  = conf->max_tasks_per_node;
	conf_ptr->mcs_plugin          = xstrdup(conf->mcs_plugin);
	conf_ptr->mcs_plugin_params   = xstrdup(conf->mcs_plugin_params);
	conf_ptr->metrics_type = xstrdup(conf->metrics_type);
	conf_ptr->min_job_age         = conf->min_job_age;
	conf_ptr->mpi_conf = mpi_g_conf_get_printable();
	conf_ptr->mpi_default         = xstrdup(conf->mpi_default);
	conf_ptr->mpi_params          = xstrdup(conf->mpi_params);
	conf_ptr->msg_timeout         = conf->msg_timeout;

	conf_ptr->next_job_id         = next_job_id;
	conf_ptr->node_features_conf  = node_features_g_get_config();
	conf_ptr->node_features_plugins = xstrdup(conf->node_features_plugins);

	conf_ptr->over_time_limit     = conf->over_time_limit;

	conf_ptr->plugindir           = xstrdup(conf->plugindir);
	conf_ptr->plugstack           = xstrdup(conf->plugstack);

	conf_ptr->preempt_mode        = conf->preempt_mode;
	conf_ptr->preempt_params = xstrdup(conf->preempt_params);
	conf_ptr->preempt_type        = xstrdup(conf->preempt_type);
	conf_ptr->preempt_exempt_time = conf->preempt_exempt_time;
	conf_ptr->prep_params         = xstrdup(conf->prep_params);
	conf_ptr->prep_plugins        = xstrdup(conf->prep_plugins);
	conf_ptr->priority_decay_hl   = conf->priority_decay_hl;
	conf_ptr->priority_calc_period = conf->priority_calc_period;
	conf_ptr->priority_favor_small= conf->priority_favor_small;
	conf_ptr->priority_flags      = conf->priority_flags;
	conf_ptr->priority_max_age    = conf->priority_max_age;
	conf_ptr->priority_params     = xstrdup(conf->priority_params);
	conf_ptr->priority_reset_period = conf->priority_reset_period;
	conf_ptr->priority_type       = xstrdup(conf->priority_type);
	conf_ptr->priority_weight_age = conf->priority_weight_age;
	conf_ptr->priority_weight_assoc = conf->priority_weight_assoc;
	conf_ptr->priority_weight_fs  = conf->priority_weight_fs;
	conf_ptr->priority_weight_js  = conf->priority_weight_js;
	conf_ptr->priority_weight_part= conf->priority_weight_part;
	conf_ptr->priority_weight_qos = conf->priority_weight_qos;
	conf_ptr->priority_weight_tres = xstrdup(conf->priority_weight_tres);

	conf_ptr->private_data        = conf->private_data;
	conf_ptr->proctrack_type      = xstrdup(conf->proctrack_type);
	conf_ptr->prolog_cnt = conf->prolog_cnt;
	conf_ptr->prolog = xcalloc(conf->prolog_cnt, sizeof(char *));
	for (int i = 0; i < conf_ptr->prolog_cnt; i++)
		conf_ptr->prolog[i] = xstrdup(conf->prolog[i]);
	conf_ptr->prolog_slurmctld_cnt = conf->prolog_slurmctld_cnt;
	conf_ptr->prolog_slurmctld = xcalloc(conf->prolog_slurmctld_cnt,
					     sizeof(char *));
	for (int i = 0; i < conf_ptr->prolog_slurmctld_cnt; i++)
		conf_ptr->prolog_slurmctld[i] =
			xstrdup(conf->prolog_slurmctld[i]);
	conf_ptr->prolog_timeout = conf->prolog_timeout;
	conf_ptr->prolog_flags        = conf->prolog_flags;
	conf_ptr->propagate_prio_process = slurm_conf.propagate_prio_process;
	conf_ptr->propagate_rlimits   = xstrdup(conf->propagate_rlimits);
	conf_ptr->propagate_rlimits_except = xstrdup(conf->
						     propagate_rlimits_except);

	conf_ptr->reboot_program      = xstrdup(conf->reboot_program);
	conf_ptr->reconfig_flags      = conf->reconfig_flags;
	conf_ptr->requeue_exit        = xstrdup(conf->requeue_exit);
	conf_ptr->requeue_exit_hold   = xstrdup(conf->requeue_exit_hold);
	conf_ptr->resume_fail_program = xstrdup(conf->resume_fail_program);
	conf_ptr->resume_program      = xstrdup(conf->resume_program);
	conf_ptr->resume_rate         = conf->resume_rate;
	conf_ptr->resume_timeout      = conf->resume_timeout;
	conf_ptr->resv_epilog         = xstrdup(conf->resv_epilog);
	conf_ptr->resv_over_run       = conf->resv_over_run;
	conf_ptr->resv_prolog         = xstrdup(conf->resv_prolog);
	conf_ptr->ret2service         = conf->ret2service;

	conf_ptr->sched_params        = xstrdup(conf->sched_params);
	conf_ptr->sched_logfile       = xstrdup(conf->sched_logfile);
	conf_ptr->sched_log_level     = conf->sched_log_level;
	conf_ptr->sched_time_slice    = conf->sched_time_slice;
	conf_ptr->schedtype           = xstrdup(conf->schedtype);
	conf_ptr->scron_params = xstrdup(conf->scron_params);
	conf_ptr->select_type         = xstrdup(conf->select_type);
	conf_ptr->select_type_param   = conf->select_type_param;
	conf_ptr->site_factor_params  = xstrdup(conf->site_factor_params);
	conf_ptr->site_factor_plugin  = xstrdup(conf->site_factor_plugin);
	conf_ptr->slurm_user_id       = conf->slurm_user_id;
	conf_ptr->slurm_user_name     = xstrdup(conf->slurm_user_name);
	conf_ptr->slurmctld_addr      = xstrdup(conf->slurmctld_addr);
	conf_ptr->slurmctld_debug     = conf->slurmctld_debug;
	conf_ptr->slurmctld_logfile   = xstrdup(conf->slurmctld_logfile);
	conf_ptr->slurmctld_params    = xstrdup(conf->slurmctld_params);
	conf_ptr->slurmctld_pidfile   = xstrdup(conf->slurmctld_pidfile);
	conf_ptr->slurmctld_port      = conf->slurmctld_port;
	conf_ptr->slurmctld_port_count = conf->slurmctld_port_count;
	conf_ptr->slurmctld_primary_off_prog  =
		xstrdup(conf->slurmctld_primary_off_prog);
	conf_ptr->slurmctld_primary_on_prog  =
		xstrdup(conf->slurmctld_primary_on_prog);
	conf_ptr->slurmctld_syslog_debug = conf->slurmctld_syslog_debug;
	conf_ptr->slurmctld_timeout   = conf->slurmctld_timeout;
	conf_ptr->slurmd_debug        = conf->slurmd_debug;
	conf_ptr->slurmd_logfile      = xstrdup(conf->slurmd_logfile);
	conf_ptr->slurmd_params	      = xstrdup(conf->slurmd_params);
	conf_ptr->slurmd_pidfile      = xstrdup(conf->slurmd_pidfile);
	conf_ptr->slurmd_port         = conf->slurmd_port;
	conf_ptr->slurmd_spooldir     = xstrdup(conf->slurmd_spooldir);
	conf_ptr->slurmd_syslog_debug = conf->slurmd_syslog_debug;
	conf_ptr->slurmd_timeout      = conf->slurmd_timeout;
	conf_ptr->slurmd_user_id      = conf->slurmd_user_id;
	conf_ptr->slurmd_user_name    = xstrdup(conf->slurmd_user_name);
	conf_ptr->slurm_conf          = xstrdup(conf->slurm_conf);
	conf_ptr->srun_epilog         = xstrdup(conf->srun_epilog);

	conf_ptr->srun_port_range = xmalloc(2 * sizeof(uint16_t));
	if (conf->srun_port_range) {
		conf_ptr->srun_port_range[0] = conf->srun_port_range[0];
		conf_ptr->srun_port_range[1] = conf->srun_port_range[1];
	} else {
		conf_ptr->srun_port_range[0] = 0;
		conf_ptr->srun_port_range[1] = 0;
	}

	conf_ptr->srun_prolog         = xstrdup(conf->srun_prolog);
	conf_ptr->state_save_location = xstrdup(conf->state_save_location);
	conf_ptr->suspend_exc_nodes   = xstrdup(conf->suspend_exc_nodes);
	conf_ptr->suspend_exc_parts   = xstrdup(conf->suspend_exc_parts);
	conf_ptr->suspend_exc_states  = xstrdup(conf->suspend_exc_states);
	conf_ptr->suspend_program     = xstrdup(conf->suspend_program);
	conf_ptr->suspend_rate        = conf->suspend_rate;
	conf_ptr->suspend_time        = conf->suspend_time;
	conf_ptr->suspend_timeout     = conf->suspend_timeout;
	conf_ptr->switch_param        = xstrdup(conf->switch_param);
	conf_ptr->switch_type         = xstrdup(conf->switch_type);

	conf_ptr->task_epilog         = xstrdup(conf->task_epilog);
	conf_ptr->task_prolog         = xstrdup(conf->task_prolog);
	conf_ptr->task_plugin         = xstrdup(conf->task_plugin);
	conf_ptr->task_plugin_param   = conf->task_plugin_param;
	conf_ptr->tcp_timeout         = conf->tcp_timeout;
	conf_ptr->tls_params = xstrdup(conf->tls_params);
	conf_ptr->tls_type = xstrdup(conf->tls_type);
	conf_ptr->tmp_fs              = xstrdup(conf->tmp_fs);
	conf_ptr->topology_param      = xstrdup(conf->topology_param);
	conf_ptr->topology_plugin     = xstrdup(conf->topology_plugin);
	conf_ptr->tree_width          = conf->tree_width;

	conf_ptr->wait_time           = conf->wait_time;

	conf_ptr->unkillable_program  = xstrdup(conf->unkillable_program);
	conf_ptr->unkillable_timeout  = conf->unkillable_timeout;
	conf_ptr->url_parser_type = xstrdup(conf->url_parser_type);
	conf_ptr->version             = xstrdup(SLURM_VERSION_STRING);
	conf_ptr->vsize_factor        = conf->vsize_factor;
	conf_ptr->x11_params          = xstrdup(conf->x11_params);
}

/*
 * validate_super_user - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_SUPER_USER level
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_super_user(uid_t uid)
{
	if ((uid == 0) || (uid == slurm_conf.slurm_user_id) ||
	    assoc_mgr_get_admin_level(acct_db_conn, uid) >=
	    SLURMDB_ADMIN_SUPER_USER)
		return true;
	else
		return false;
}

/*
 * validate_operator - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_OPERATOR level
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
static bool _validate_operator_internal(uid_t uid, bool locked)
{
	slurmdb_admin_level_t level;

	if ((uid == 0) || (uid == slurm_conf.slurm_user_id))
		return true;

	if (locked)
		level = assoc_mgr_get_admin_level_locked(acct_db_conn, uid);
	else
		level = assoc_mgr_get_admin_level(acct_db_conn, uid);

	if (level >= SLURMDB_ADMIN_OPERATOR)
		return true;

	return false;
}

extern bool validate_operator(uid_t uid)
{
	return _validate_operator_internal(uid, false);
}

extern bool validate_operator_locked(uid_t uid)
{
	return _validate_operator_internal(uid, true);
}

extern bool validate_operator_user_rec(slurmdb_user_rec_t *user)
{
	if ((user->uid == 0) ||
	    (user->uid == slurm_conf.slurm_user_id) ||
	    (user->admin_level >= SLURMDB_ADMIN_OPERATOR))
		return true;
	else
		return false;

}

static void _set_identity(slurm_msg_t *msg, void **id)
{
	static bool set = false, use_client_ids = false;

	if (!set) {
		if (xstrstr(slurm_conf.authinfo, "use_client_ids"))
			use_client_ids = true;
		set = true;
	}

	if (!use_client_ids)
		return;

	*id = (void *) auth_g_get_identity(msg->auth_cred);
}

static void _set_hostname(slurm_msg_t *msg, char **alloc_node)
{
	xfree(*alloc_node);
	*alloc_node = auth_g_get_host(msg);
}

static int _valid_id(char *caller, job_desc_msg_t *msg, uid_t uid, gid_t gid,
		     uint16_t protocol_version)
{
	if ((msg->user_id == NO_VAL) || (msg->group_id == NO_VAL)) {
		/*
		 * Catch and reject NO_VAL.
		 */
		error("%s: rejecting requested UID=NO_VAL or GID=NO_VAL as invalid",
		      caller);
		return ESLURM_USER_ID_MISSING;
	}

	/*
	 * If UID/GID not given use the authenticated values.
	 */
	if (msg->user_id == SLURM_AUTH_NOBODY)
		msg->user_id = uid;
	if (msg->group_id == SLURM_AUTH_NOBODY)
		msg->group_id = gid;

	if (validate_slurm_user(uid))
		return SLURM_SUCCESS;

	if (uid != msg->user_id) {
		error("%s: Requested UID=%u doesn't match user UID=%u.",
		      caller, msg->user_id, uid);
		return ESLURM_USER_ID_MISSING;
	}

	/* if GID not given, then use GID from auth */
	if (gid != msg->group_id) {
		error("%s: Requested GID=%u doesn't match user GID=%u.",
		      caller, msg->group_id, gid);
		return ESLURM_GROUP_ID_MISSING;
	}

	return SLURM_SUCCESS;
}

extern void configless_update(void)
{
	if (!xstrcasestr(slurm_conf.slurmctld_params, "enable_configless"))
		return;

	grab_include_directives();
	running_configless = true;
	slurm_rwlock_wrlock(&configless_lock);
	slurm_free_config_response_msg(config_for_slurmd);
	slurm_free_config_response_msg(config_for_clients);

	config_for_slurmd = new_config_response(true);
	config_for_slurmd->slurmd_spooldir = xstrdup(slurm_conf.slurmd_spooldir);
	config_for_clients = new_config_response(false);
	slurm_rwlock_unlock(&configless_lock);
}

extern void configless_clear(void)
{
	slurm_rwlock_wrlock(&configless_lock);
	slurm_free_config_response_msg(config_for_slurmd);
	slurm_free_config_response_msg(config_for_clients);

	FREE_NULL_LIST(conf_includes_list);
	slurm_rwlock_unlock(&configless_lock);
}

/* _kill_job_on_msg_fail - The request to create a job record succeeded,
 *	but the reply message to srun failed. We kill the job to avoid
 *	leaving it orphaned */
static void _kill_job_on_msg_fail(uint32_t job_id)
{
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	slurm_step_id_t step_id = {
		.job_id = job_id,
	};

	error("Job allocate response msg send failure, killing JobId=%u",
	      job_id);
	lock_slurmctld(job_write_lock);
	job_complete(&step_id, slurm_conf.slurm_user_id, false, false, SIGTERM);
	unlock_slurmctld(job_write_lock);
}

static int _het_job_cancel(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	time_t now = time(NULL);

	info("Cancelling aborted hetjob submit: %pJ", job_ptr);
	job_state_set(job_ptr, JOB_CANCELLED);
	job_ptr->start_time	= now;
	job_ptr->end_time	= now;
	job_ptr->exit_code	= 1;
	job_completion_logger(job_ptr, false);
	fed_mgr_job_complete(job_ptr, 0, now);

	return 0;
}

/*
 * build_alloc_msg - Fill in resource_allocation_response_msg_t off job_record.
 * job_ptr IN - job_record to copy members off.
 * error_code IN - error code used for the response.
 * job_submit_user_msg IN - user message from job submit plugin.
 * RET resource_allocation_response_msg_t filled in.
 */
extern resource_allocation_response_msg_t *build_alloc_msg(
	job_record_t *job_ptr, int error_code, char *job_submit_user_msg)
{
	int i;
	resource_allocation_response_msg_t *alloc_msg =
		xmalloc(sizeof(resource_allocation_response_msg_t));

	/* send job_ID and node_name_ptr */
	if (job_ptr->job_resrcs && job_ptr->job_resrcs->cpu_array_cnt) {
		alloc_msg->num_cpu_groups = job_ptr->job_resrcs->cpu_array_cnt;
		alloc_msg->cpu_count_reps = xmalloc(sizeof(uint32_t) *
						    job_ptr->job_resrcs->
						    cpu_array_cnt);
		memcpy(alloc_msg->cpu_count_reps,
		       job_ptr->job_resrcs->cpu_array_reps,
		       (sizeof(uint32_t) * job_ptr->job_resrcs->cpu_array_cnt));
		alloc_msg->cpus_per_node  = xmalloc(sizeof(uint16_t) *
						    job_ptr->job_resrcs->
						    cpu_array_cnt);
		memcpy(alloc_msg->cpus_per_node,
		       job_ptr->job_resrcs->cpu_array_value,
		       (sizeof(uint16_t) * job_ptr->job_resrcs->cpu_array_cnt));
	}

	alloc_msg->error_code     = error_code;
	alloc_msg->job_submit_user_msg = xstrdup(job_submit_user_msg);
	alloc_msg->step_id = STEP_ID_FROM_JOB_RECORD(job_ptr);
	alloc_msg->node_cnt       = job_ptr->node_cnt;
	alloc_msg->node_list      = xstrdup(job_ptr->nodes);
	if (job_ptr->part_ptr)
		alloc_msg->partition = xstrdup(job_ptr->part_ptr->name);
	else
		alloc_msg->partition = xstrdup(job_ptr->partition);
	alloc_msg->batch_host = xstrdup(job_ptr->batch_host);
	if (job_ptr->details) {
		if (job_ptr->bit_flags & JOB_MEM_SET) {
			alloc_msg->pn_min_memory =
				job_ptr->details->pn_min_memory;
		}
		alloc_msg->cpu_freq_min = job_ptr->details->cpu_freq_min;
		alloc_msg->cpu_freq_max = job_ptr->details->cpu_freq_max;
		alloc_msg->cpu_freq_gov = job_ptr->details->cpu_freq_gov;
		alloc_msg->ntasks_per_tres = job_ptr->details->ntasks_per_tres;
		alloc_msg->segment_size = job_ptr->details->segment_size;
		if (job_ptr->details->mc_ptr) {
			alloc_msg->ntasks_per_board =
				job_ptr->details->mc_ptr->ntasks_per_board;
			alloc_msg->ntasks_per_core =
				job_ptr->details->mc_ptr->ntasks_per_core;
			alloc_msg->ntasks_per_socket =
				job_ptr->details->mc_ptr->ntasks_per_socket;
		}

		if (job_ptr->details->env_cnt) {
			alloc_msg->env_size = job_ptr->details->env_cnt;
			alloc_msg->environment =
				xcalloc(alloc_msg->env_size + 1,
					sizeof(char *));
			for (i = 0; i < alloc_msg->env_size; i++) {
				alloc_msg->environment[i] =
					xstrdup(job_ptr->details->env_sup[i]);
			}
		}
		if (job_ptr->bit_flags & STEPMGR_ENABLED) {
			env_array_overwrite(&alloc_msg->environment,
					    "SLURM_STEPMGR",
					    job_ptr->batch_host);
			alloc_msg->env_size =
				PTR_ARRAY_SIZE(alloc_msg->environment) - 1;
		}
	} else {
		/* alloc_msg->pn_min_memory = 0; */
		alloc_msg->ntasks_per_board  = NO_VAL16;
		alloc_msg->ntasks_per_core   = NO_VAL16;
		alloc_msg->ntasks_per_tres    = NO_VAL16;
		alloc_msg->ntasks_per_socket = NO_VAL16;
	}
	if (job_ptr->account)
		alloc_msg->account = xstrdup(job_ptr->account);
	if (job_ptr->qos_ptr) {
		slurmdb_qos_rec_t *qos;
		qos = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;
		alloc_msg->qos = xstrdup(qos->name);
	}
	if (job_ptr->resv_name)
		alloc_msg->resv_name = xstrdup(job_ptr->resv_name);

	set_remote_working_response(alloc_msg, job_ptr,
				    job_ptr->origin_cluster);

	alloc_msg->tres_per_node = xstrdup(job_ptr->tres_per_node);
	alloc_msg->tres_per_task = xstrdup(job_ptr->tres_per_task);
	alloc_msg->uid = job_ptr->user_id;
	alloc_msg->user_name = user_from_job(job_ptr);
	alloc_msg->gid = job_ptr->group_id;
	alloc_msg->group_name = group_from_job(job_ptr);
	alloc_msg->start_protocol_ver = job_ptr->start_protocol_ver;

	return alloc_msg;
}

static void _del_alloc_het_job_msg(void *x)
{
	resource_allocation_response_msg_t *alloc_msg;

	alloc_msg = (resource_allocation_response_msg_t *) x;
	/* NULL out working_cluster_rec since it's pointing to global memory */
	alloc_msg->working_cluster_rec = NULL;
	slurm_free_resource_allocation_response_msg(alloc_msg);
}

static bool _sched_backfill(void)
{
	static int backfill = -1;

	if (backfill == -1) {
		if (!xstrcmp(slurm_conf.schedtype, "sched/backfill"))
			backfill = 1;
		else
			backfill = 0;
	}

	if (backfill)
		return true;
	return false;
}

/*
 * If any job component has required nodes, those nodes must be excluded
 * from all other components to avoid scheduling deadlock
*/
static void _exclude_het_job_nodes(list_t *job_req_list)
{
	job_desc_msg_t *job_desc_msg;
	list_itr_t *iter;
	int het_job_cnt, req_cnt = 0, i;
	char **req_nodes, *sep;

	het_job_cnt = list_count(job_req_list);
	req_nodes = xmalloc(sizeof(char *) * het_job_cnt);
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = list_next(iter))) {
		if (!job_desc_msg->req_nodes || !job_desc_msg->req_nodes[0])
			continue;
		req_nodes[req_cnt++] = job_desc_msg->req_nodes;
	}
	if (req_cnt) {
		list_iterator_reset(iter);
		while ((job_desc_msg = list_next(iter))) {
			for (i = 0; i < req_cnt; i++) {
				if (req_nodes[i] == job_desc_msg->req_nodes)
					continue;     /* required by this job */
				if (job_desc_msg->exc_nodes &&
				    job_desc_msg->exc_nodes[0])
					sep = ",";
				else
					sep = "";
				xstrfmtcat(job_desc_msg->exc_nodes, "%s%s",
					   sep, req_nodes[i]);
			}
		}
	}
	list_iterator_destroy(iter);
	xfree(req_nodes);
}

/*
 * _create_het_job_id_set - Obtain the het_job_id_set
 * het_job_id_set OUT - allocated in the function and must be xfreed
 *                       be the caller.
 */
static void _create_het_job_id_set(hostset_t *jobid_hostset,
				    uint32_t het_job_offset,
				    char **het_job_id_set)
{
	char *tmp_str = NULL;
	char *tmp_offset = NULL;

	if (!jobid_hostset)
		return;

	tmp_str = hostset_ranged_string_xmalloc(jobid_hostset);
	tmp_offset = tmp_str;
	if (tmp_str[0] == '[') {
		tmp_offset = strchr(tmp_str, ']');
		if (tmp_offset)
			tmp_offset[0] = '\0';
		tmp_offset = tmp_str + 1;
	}

	*het_job_id_set = xstrdup(tmp_offset);
	xfree(tmp_str);
}

/* _slurm_rpc_allocate_het_job: process RPC to allocate a hetjob resources */
static void _slurm_rpc_allocate_het_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS, inx, het_job_cnt = -1;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg;
	list_t *job_req_list = msg->data;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	job_record_t *job_ptr, *first_job_ptr = NULL;
	char *err_msg = NULL, **job_submit_user_msg = NULL;
	list_itr_t *iter;
	list_t *submit_job_list = NULL;
	uint32_t het_job_id = 0, het_job_offset = 0;
	hostset_t *jobid_hostset = NULL;
	char tmp_str[32];
	list_t *resp = NULL;
	char resp_host[INET6_ADDRSTRLEN];
	char *het_job_id_set = NULL;

	START_TIMER;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		goto send_msg;
	}
	if (!_sched_backfill()) {
		info("REQUEST_HET_JOB_ALLOCATION from uid=%u rejected as sched/backfill is not configured",
		     msg->auth_uid);
		error_code = ESLURM_NOT_SUPPORTED;
		goto send_msg;
	}
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_HET_JOB_ALLOCATION from uid=%u with empty job list",
		     msg->auth_uid);
		error_code = SLURM_ERROR;
		goto send_msg;
	}
	if (msg->address.ss_family != AF_UNSPEC) {
		slurm_get_ip_str(&msg->address, resp_host, sizeof(resp_host));
	} else {
		info("REQUEST_HET_JOB_ALLOCATION from uid=%u, can't get peer addr",
		     msg->auth_uid);
		error_code = SLURM_ERROR;
		goto send_msg;
	}

	sched_debug3("Processing RPC: REQUEST_HET_JOB_ALLOCATION from uid=%u",
		     msg->auth_uid);

	/*
	 * If any job component has required nodes, those nodes must be excluded
	 * from all other components to avoid scheduling deadlock
	 */
	_exclude_het_job_nodes(job_req_list);

	het_job_cnt = list_count(job_req_list);
	job_submit_user_msg = xmalloc(sizeof(char *) * het_job_cnt);
	submit_job_list = list_create(NULL);
	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	inx = 0;
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = list_next(iter))) {
		/*
		 * Ignore what was sent in the RPC, only use auth values.
		 */
		job_desc_msg->user_id = msg->auth_uid;
		job_desc_msg->group_id = msg->auth_gid;

		_set_hostname(msg, &job_desc_msg->alloc_node);

		if ((job_desc_msg->alloc_node == NULL) ||
		    (job_desc_msg->alloc_node[0] == '\0')) {
			error_code = ESLURM_INVALID_NODE_NAME;
			error("REQUEST_HET_JOB_ALLOCATION lacks alloc_node from uid=%u",
			      msg->auth_uid);
			break;
		}

		if (job_desc_msg->array_inx) {
			error_code = ESLURM_INVALID_ARRAY;
			break;
		}

		if (job_desc_msg->immediate) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			break;
		}

		/* Locks are for job_submit plugin use */
		job_desc_msg->het_job_offset = het_job_offset;
		error_code = validate_job_create_req(job_desc_msg, msg->auth_uid,
						     &job_submit_user_msg[inx]);
		if (error_code)
			break;

		dump_job_desc(job_desc_msg);

		job_ptr = NULL;
		if (!job_desc_msg->resp_host)
			job_desc_msg->resp_host = xstrdup(resp_host);
		if (het_job_offset) {
			/*
			 * Email notifications disable except for the
			 * hetjob leader
			 */
			job_desc_msg->mail_type = 0;
			xfree(job_desc_msg->mail_user);

			/* license request allowed only on leader */
			if (job_desc_msg->licenses) {
				xstrfmtcat(job_submit_user_msg[inx],
					   "%slicense request allowed only on leader job",
					   job_submit_user_msg[inx] ? "\n" : "");
				error("REQUEST_HET_JOB_ALLOCATION from uid=%u, license request on non-leader job",
				      msg->auth_uid);
				error_code = ESLURM_INVALID_LICENSES;
				break;
			}
		}
		job_desc_msg->het_job_offset = het_job_offset;
		error_code = job_allocate(job_desc_msg, false, false, NULL,
					  true, msg->auth_uid, false, &job_ptr,
					  &err_msg, msg->protocol_version);
		if (!job_ptr) {
			if (error_code == SLURM_SUCCESS)
				error_code = SLURM_ERROR;
			break;
		}
		if (error_code && (job_ptr->job_state == JOB_FAILED))
			break;
		error_code = SLURM_SUCCESS;	/* Non-fatal error */
		if (het_job_id == 0) {
			het_job_id = job_ptr->job_id;
			first_job_ptr = job_ptr;
		}
		snprintf(tmp_str, sizeof(tmp_str), "%u", job_ptr->job_id);
		if (jobid_hostset)
			hostset_insert(jobid_hostset, tmp_str);
		else
			jobid_hostset = hostset_create(tmp_str);
		job_ptr->het_job_id     = het_job_id;
		job_ptr->het_job_offset = het_job_offset++;
		on_job_state_change(job_ptr, job_ptr->job_state);
		list_append(submit_job_list, job_ptr);
		inx++;
	}
	list_iterator_destroy(iter);

	if ((error_code == 0) && (!first_job_ptr)) {
		error("%s: No error, but no het_job_id", __func__);
		error_code = SLURM_ERROR;
	}

	/* Validate limits on hetjob as a whole */
	if ((error_code == SLURM_SUCCESS) &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    !acct_policy_validate_het_job(submit_job_list)) {
		info("Hetjob %u exceeded association/QOS limit for user %u",
		     het_job_id, msg->auth_uid);
		error_code = ESLURM_ACCOUNTING_POLICY;
        }

	/* Set the het_job_id_set */
	_create_het_job_id_set(jobid_hostset, het_job_offset,
				&het_job_id_set);

	if (first_job_ptr)
		first_job_ptr->het_job_list = submit_job_list;
	iter = list_iterator_create(submit_job_list);
	while ((job_ptr = list_next(iter))) {
		job_ptr->het_job_id_set = xstrdup(het_job_id_set);
	}
	list_iterator_destroy(iter);
	xfree(het_job_id_set);

	if (error_code) {
		/* Cancel remaining job records */
		(void) list_for_each(submit_job_list, _het_job_cancel, NULL);
		if (!first_job_ptr)
			FREE_NULL_LIST(submit_job_list);
	} else {
		list_itr_t *iter;
		inx = 0;
		iter = list_iterator_create(submit_job_list);
		while ((job_ptr = list_next(iter))) {
			if (!resp)
				resp = list_create(_del_alloc_het_job_msg);
			list_append(resp,
				    build_alloc_msg(
					    job_ptr, error_code,
					    job_submit_user_msg[inx++]));
			log_flag(HETJOB, "Submit %pJ", job_ptr);
		}
		list_iterator_destroy(iter);
	}
	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);
	END_TIMER2(__func__);

	if (resp) {
		if (send_msg_response(msg, RESPONSE_HET_JOB_ALLOCATION, resp))
			_kill_job_on_msg_fail(het_job_id);
		FREE_NULL_LIST(resp);
	} else {
		char *aggregate_user_msg;

send_msg:	info("%s: %s ", __func__, slurm_strerror(error_code));
		aggregate_user_msg = NULL;

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		for (inx = 0; inx < het_job_cnt; inx++) {
			char *line = NULL, *last = NULL;

			if (!job_submit_user_msg[inx])
				continue;

			/*
			 * Break apart any combined sentences and tag with index
			 */
			line = strtok_r(job_submit_user_msg[inx], "\n", &last);
			while (line) {
				xstrfmtcat(aggregate_user_msg, "%s%d: %s",
					   (aggregate_user_msg ? "\n" : ""),
					    inx, line);

				line = strtok_r(NULL, "\n", &last);
			}
		}
		if (aggregate_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = aggregate_user_msg;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	}
	xfree(err_msg);
	for (inx = 0; inx < het_job_cnt; inx++)
		xfree(job_submit_user_msg[inx]);
	xfree(job_submit_user_msg);

	if (jobid_hostset)
		hostset_destroy(jobid_hostset);

	schedule_job_save();	/* has own locks */
}

/* _slurm_rpc_allocate_resources:  process RPC to allocate resources for
 *	a job
 */
static void _slurm_rpc_allocate_resources(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg = msg->data;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	int immediate = job_desc_msg->immediate;
	bool do_unlock = false;
	bool reject_job = false;
	job_record_t *job_ptr = NULL;
	char *err_msg = NULL, *job_submit_user_msg = NULL;

	START_TIMER;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}

	/*
	 * Ignore what was sent in the RPC, only use auth values.
	 */
	job_desc_msg->user_id = msg->auth_uid;
	job_desc_msg->group_id = msg->auth_gid;

	sched_debug3("Processing RPC: REQUEST_RESOURCE_ALLOCATION from uid=%u",
		     msg->auth_uid);

	_set_hostname(msg, &job_desc_msg->alloc_node);
	_set_identity(msg, &job_desc_msg->id);

	/* do RPC call */
	if ((job_desc_msg->alloc_node == NULL) ||
	    (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_RESOURCE_ALLOCATE lacks alloc_node from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		lock_slurmctld(job_read_lock);
		job_desc_msg->het_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,
						     msg->auth_uid, &err_msg);
		unlock_slurmctld(job_read_lock);
	}

	/*
	 * In validate_job_create_req(), err_msg is currently only modified in
	 * the call to job_submit_g_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to fed_mgr_job_allocate and/or job_allocate, and we
	 * need the job submit plugin value to build the resource allocation
	 * response in the call to build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	if (error_code) {
		reject_job = true;
	} else if (msg->address.ss_family != AF_UNSPEC) {
		/* resp_host could already be set from a federated cluster */
		if (!job_desc_msg->resp_host) {
			job_desc_msg->resp_host = xmalloc(INET6_ADDRSTRLEN);
			slurm_get_ip_str(&msg->address, job_desc_msg->resp_host,
					 INET6_ADDRSTRLEN);
		}
		dump_job_desc(job_desc_msg);
		do_unlock = true;
		_throttle_start(&active_rpc_cnt);

		lock_slurmctld(job_write_lock);
		if (fed_mgr_fed_rec) {
			uint32_t job_id;
			if (fed_mgr_job_allocate(msg, job_desc_msg, true,
						 &job_id, &error_code,
						 &err_msg)) {
				reject_job = true;
			} else if (!(job_ptr = find_job_record(job_id))) {
				error("%s: can't find fed job that was just created. this should never happen",
				      __func__);
				reject_job = true;
				error_code = SLURM_ERROR;
			}
		} else {
			job_desc_msg->het_job_offset = NO_VAL;
			error_code = job_allocate(job_desc_msg, immediate,
						  false, NULL, true,
						  msg->auth_uid, false,
						  &job_ptr, &err_msg,
						  msg->protocol_version);
			/* unlock after finished using the job structure
			 * data */

			/* return result */
			if (!job_ptr ||
			    (error_code && job_ptr->job_state == JOB_FAILED))
				reject_job = true;
		}
		END_TIMER2(__func__);
	} else {
		reject_job = true;
		error_code = SLURM_UNKNOWN_FORWARD_ADDR;
	}

send_msg:

	if (!reject_job) {
		resource_allocation_response_msg_t *alloc_msg =
			build_alloc_msg(job_ptr, error_code,
					job_submit_user_msg);

		xassert(job_ptr);
		sched_info("%s %pJ NodeList=%s %s",
			   __func__, job_ptr, job_ptr->nodes, TIME_STR);

		/*
		 * This check really isn't needed, but just doing it
		 * to be more complete.
		 */
		if (do_unlock) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}

		if (send_msg_response(msg, RESPONSE_RESOURCE_ALLOCATION,
				      alloc_msg))
			_kill_job_on_msg_fail(job_ptr->job_id);

		schedule_job_save();	/* has own locks */
		schedule_node_save();	/* has own locks */

		if (!alloc_msg->node_cnt) /* didn't get an allocation */
			queue_job_scheduler();

		/* NULL out working_cluster_rec since it's pointing to global
		 * memory */
		alloc_msg->working_cluster_rec = NULL;
		slurm_free_resource_allocation_response_msg(alloc_msg);
	} else {	/* allocate error */
		if (do_unlock) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		info("%s: %s ", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_dump_conf - process RPC for Slurm configuration information */
static void _slurm_rpc_dump_conf(slurm_msg_t *msg)
{
	DEF_TIMERS;
	last_update_msg_t *last_time_msg = msg->data;
	slurm_ctl_conf_info_msg_t config_tbl;
	/* Locks: Read config, job, partition, fed */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	lock_slurmctld(config_read_lock);

	/* check to see if configuration data has changed */
	if ((last_time_msg->last_update - 1) >= slurm_conf.last_update) {
		unlock_slurmctld(config_read_lock);
		debug2("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		_fill_ctld_conf(&config_tbl);
		unlock_slurmctld(config_read_lock);
		END_TIMER2(__func__);

		/* send message */
		(void) send_msg_response(msg, RESPONSE_BUILD_INFO, &config_tbl);
		free_slurm_conf(&config_tbl, false);
	}
}

/* _slurm_rpc_dump_jobs - process RPC for job state information */
static void _slurm_rpc_dump_jobs(slurm_msg_t *msg)
{
	DEF_TIMERS;
	buf_t *buffer = NULL;
	job_info_request_msg_t *job_info_request_msg = msg->data;
	/* Locks: Read config job part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_read_lock);

	if ((job_info_request_msg->last_update - 1) >= last_job_update) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_read_lock);
		debug3("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		if (job_info_request_msg->job_ids) {
			buffer = pack_spec_jobs(job_info_request_msg->job_ids,
						job_info_request_msg->show_flags,
						msg->auth_uid, NO_VAL,
						msg->protocol_version);
		} else {
			buffer = pack_all_jobs(job_info_request_msg->show_flags,
					       msg->auth_uid, NO_VAL,
					       msg->protocol_version);
		}
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_read_lock);
		END_TIMER2(__func__);
#if 0
		info("%s, size=%d %s", __func__, dump_size, TIME_STR);
#endif

		/* send message */
		(void) send_msg_response(msg, RESPONSE_JOB_INFO, buffer);
		FREE_NULL_BUFFER(buffer);
	}
}

/* _slurm_rpc_dump_jobs - process RPC for job state information */
static void _slurm_rpc_dump_jobs_user(slurm_msg_t *msg)
{
	DEF_TIMERS;
	buf_t *buffer = NULL;
	job_user_id_msg_t *job_info_request_msg = msg->data;
	/* Locks: Read config job part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_read_lock);
	buffer = pack_all_jobs(job_info_request_msg->show_flags, msg->auth_uid,
			       job_info_request_msg->user_id,
			       msg->protocol_version);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);
#if 0
	info("%s, size=%d %s", __func__, dump_size, TIME_STR);
#endif

	/* send message */
	(void) send_msg_response(msg, RESPONSE_JOB_INFO, buffer);
	FREE_NULL_BUFFER(buffer);
}

static void _slurm_rpc_job_state(slurm_msg_t *msg)
{
	DEF_TIMERS;
	job_state_request_msg_t *js = msg->data;
	job_state_response_msg_t *jsr = NULL;
	int rc;


	jsr = xmalloc(sizeof(*jsr));

	START_TIMER;

	/* Do not lock here. Locking is done conditionally in dump_job_state */
	rc = dump_job_state(js->count, js->job_ids, &jsr->jobs_count,
			    &jsr->jobs);

	END_TIMER2(__func__);

	if (rc) {
		slurm_send_rc_msg(msg, rc);
	} else {
		(void) send_msg_response(msg, RESPONSE_JOB_STATE, jsr);
	}

	slurm_free_job_state_response_msg(jsr);
}

/* _slurm_rpc_dump_job_single - process RPC for one job's state information */
static void _slurm_rpc_dump_job_single(slurm_msg_t *msg)
{
	DEF_TIMERS;
	buf_t *buffer = NULL;
	job_id_msg_t *job_id_msg = msg->data;
	/* Locks: Read config, job, and node info */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_read_lock);
	buffer = pack_one_job(&job_id_msg->step_id, job_id_msg->show_flags,
			      msg->auth_uid, msg->protocol_version);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (!buffer) {
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
	} else {
		(void) send_msg_response(msg, RESPONSE_JOB_INFO, buffer);
	}
	FREE_NULL_BUFFER(buffer);
}

static void _slurm_rpc_hostlist_expansion(slurm_msg_t *msg)
{
	DEF_TIMERS;
	slurmctld_lock_t node_read_lock = {
		.node = READ_LOCK,
	};
	bitstr_t *bitmap = NULL;
	char *expanded = NULL;

	START_TIMER;
	if ((slurm_conf.private_data & PRIVATE_DATA_NODES) &&
	    (!validate_operator(msg->auth_uid))) {
		error("Security violation, REQUEST_HOSTLIST_EXPANSION RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(node_read_lock);

	if (!node_name2bitmap(msg->data, false, &bitmap, NULL))
		expanded = bitmap2node_name_sortable(bitmap, false);
	FREE_NULL_BITMAP(bitmap);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(node_read_lock);
	END_TIMER2(__func__);

	if (!expanded) {
		slurm_send_rc_msg(msg, ESLURM_INVALID_NODE_NAME);
	} else {
		(void) send_msg_response(msg, RESPONSE_HOSTLIST_EXPANSION,
					 expanded);
	}
	xfree(expanded);
}

static void _slurm_rpc_get_shares(slurm_msg_t *msg)
{
	DEF_TIMERS;
	shares_request_msg_t *req_msg = msg->data;
	shares_response_msg_t resp_msg;

	START_TIMER;
	memset(&resp_msg, 0, sizeof(resp_msg));
	assoc_mgr_get_shares(acct_db_conn, msg->auth_uid, req_msg, &resp_msg);

	(void) send_msg_response(msg, RESPONSE_SHARE_INFO, &resp_msg);
	FREE_NULL_LIST(resp_msg.assoc_shares_list);
	/* don't free the resp_msg.tres_names */
	END_TIMER2(__func__);
	debug2("%s %s", __func__, TIME_STR);
}

static void _slurm_rpc_get_priority_factors(slurm_msg_t *msg)
{
	DEF_TIMERS;
	priority_factors_response_msg_t resp_msg;
	/* Read lock on jobs, nodes, and partitions */
	slurmctld_lock_t job_read_lock = {
		.job = READ_LOCK,
		.node = READ_LOCK,
		.part = READ_LOCK,
	};
	assoc_mgr_lock_t qos_read_locks = {
		.qos = READ_LOCK,
	};


	START_TIMER;
	lock_slurmctld(job_read_lock);
	assoc_mgr_lock(&qos_read_locks);

	resp_msg.priority_factors_list = priority_g_get_priority_factors_list(
		msg->auth_uid);
	(void) send_msg_response(msg, RESPONSE_PRIORITY_FACTORS, &resp_msg);
	assoc_mgr_unlock(&qos_read_locks);
	unlock_slurmctld(job_read_lock);
	FREE_NULL_LIST(resp_msg.priority_factors_list);
	END_TIMER2(__func__);
	debug2("%s %s", __func__, TIME_STR);
}

/* _slurm_rpc_end_time - Process RPC for job end time */
static void _slurm_rpc_end_time(slurm_msg_t *msg)
{
	DEF_TIMERS;
	job_alloc_info_msg_t *time_req_msg = msg->data;
	srun_timeout_msg_t timeout_msg;
	int rc;
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);
	rc = job_end_time(time_req_msg, &timeout_msg);
	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		(void) send_msg_response(msg, SRUN_TIMEOUT, &timeout_msg);
	}
	debug2("%s %pI %s", __func__, &time_req_msg->step_id, TIME_STR);
}

/* _slurm_rpc_get_fd - process RPC for federation state information */
static void _slurm_rpc_get_fed(slurm_msg_t *msg)
{
	DEF_TIMERS;
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(fed_read_lock);

	/* send message */
	(void) send_msg_response(msg, RESPONSE_FED_INFO, fed_mgr_fed_rec);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(fed_read_lock);

	END_TIMER2(__func__);
	debug2("%s %s", __func__, TIME_STR);
}

/* _slurm_rpc_dump_nodes - dump RPC for node state information */
static void _slurm_rpc_dump_nodes(slurm_msg_t *msg)
{
	DEF_TIMERS;
	buf_t *buffer;
	node_info_request_msg_t *node_req_msg = msg->data;
	/* Locks: Read config, write node (reset allocated CPU count in some
	 * select plugins), read part (for part_is_visible) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	if ((slurm_conf.private_data & PRIVATE_DATA_NODES) &&
	    (!validate_operator(msg->auth_uid))) {
		error("Security violation, REQUEST_NODE_INFO RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(node_write_lock);

	select_g_select_nodeinfo_set_all();

	if ((node_req_msg->last_update - 1) >= last_node_update) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(node_write_lock);
		debug3("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		buffer = pack_all_nodes(node_req_msg->show_flags,
					msg->auth_uid, msg->protocol_version);
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(node_write_lock);
		END_TIMER2(__func__);

		/* send message */
		(void) send_msg_response(msg, RESPONSE_NODE_INFO, buffer);
		FREE_NULL_BUFFER(buffer);
	}
}

/* _slurm_rpc_dump_node_single - done RPC state information for one node */
static void _slurm_rpc_dump_node_single(slurm_msg_t *msg)
{
	DEF_TIMERS;
	buf_t *buffer = NULL;
	node_info_single_msg_t *node_req_msg = msg->data;
	/* Locks: Read config, read node, read part (for part_is_visible) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	if ((slurm_conf.private_data & PRIVATE_DATA_NODES) &&
	    (!validate_operator(msg->auth_uid))) {
		error("Security violation, REQUEST_NODE_INFO_SINGLE RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	lock_slurmctld(node_write_lock);

#if 0
	/* This function updates each node's alloc_cpus count and too slow for
	 * our use here. Node write lock is needed if this function is used */
	select_g_select_nodeinfo_set_all();
#endif
	buffer = pack_one_node(node_req_msg->show_flags, msg->auth_uid,
			       node_req_msg->node_name, msg->protocol_version);
	unlock_slurmctld(node_write_lock);
	END_TIMER2(__func__);

	/* send message */
	(void) send_msg_response(msg, RESPONSE_NODE_INFO, buffer);
	FREE_NULL_BUFFER(buffer);
}

/* _slurm_rpc_dump_partitions - process RPC for partition state information */
static void _slurm_rpc_dump_partitions(slurm_msg_t *msg)
{
	DEF_TIMERS;
	buf_t *buffer = NULL;
	part_info_request_msg_t *part_req_msg = msg->data;

	/* Locks: Read configuration and partition */
	slurmctld_lock_t part_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	if ((slurm_conf.private_data & PRIVATE_DATA_PARTITIONS) &&
	    !validate_operator(msg->auth_uid)) {
		debug2("Security violation, PARTITION_INFO RPC from uid=%u",
		       msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(part_read_lock);

	if ((part_req_msg->last_update - 1) >= last_part_update) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(part_read_lock);
		debug2("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		buffer = pack_all_part(part_req_msg->show_flags, msg->auth_uid,
				       msg->protocol_version);
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(part_read_lock);
		END_TIMER2(__func__);

		/* send message */
		(void) send_msg_response(msg, RESPONSE_PARTITION_INFO, buffer);
		FREE_NULL_BUFFER(buffer);
	}
}

/* _slurm_rpc_epilog_complete - process RPC noting the completion of
 * the epilog denoting the completion of a job it its entirety */
static void _slurm_rpc_epilog_complete(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	static time_t config_update = 0;
	static bool defer_sched = false;
	DEF_TIMERS;
	/* Locks: Read configuration, write job, write node */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	epilog_complete_msg_t *epilog_msg = msg->data;
	job_record_t *job_ptr;
	bool run_scheduler = false;

	START_TIMER;
	if (!validate_slurm_user(msg->auth_uid)) {
		error("Security violation, EPILOG_COMPLETE RPC from uid=%u",
		      msg->auth_uid);
		return;
	}

	/* Only throttle on non-composite messages, the lock should
	 * already be set earlier. */
	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		if (config_update != slurm_conf.last_update) {
			defer_sched = (xstrcasestr(slurm_conf.sched_params,
						   "defer"));
			config_update = slurm_conf.last_update;
		}

		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	log_flag(ROUTE, "%s: node_name = %s, %pI",
		 __func__, epilog_msg->node_name, &epilog_msg->step_id);

	if (!(job_ptr = find_job(&epilog_msg->step_id)))
		error("%s: could not find %pI", __func__, &epilog_msg->step_id);
	else if (job_epilog_complete(job_ptr, epilog_msg->node_name,
				     epilog_msg->return_code))
		run_scheduler = true;

	if (epilog_msg->return_code)
		error("%s: epilog error %pJ Node=%s Err=%s %s",
		      __func__, job_ptr, epilog_msg->node_name,
		      slurm_strerror(epilog_msg->return_code), TIME_STR);
	else
		debug2("%s: %pJ Node=%s %s",
		       __func__, job_ptr, epilog_msg->node_name, TIME_STR);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

	END_TIMER2(__func__);

	/* Functions below provide their own locking */
	if (!(msg->flags & CTLD_QUEUE_PROCESSING) && run_scheduler) {
		/*
		 * In defer mode, avoid triggering the scheduler logic
		 * for every epilog complete message.
		 * As one epilog message is sent from every node of each
		 * job at termination, the number of simultaneous schedule
		 * calls can be very high for large machine or large number
		 * of managed jobs.
		 */
		if (!LOTS_OF_AGENTS && !defer_sched)
			schedule(false);	/* Has own locking */
		else
			queue_job_scheduler();
		schedule_node_save();		/* Has own locking */
		schedule_job_save();		/* Has own locking */
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

/* _slurm_rpc_job_step_kill - process RPC to cancel an entire job or
 * an individual job step */
static void _slurm_rpc_job_step_kill(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	job_step_kill_msg_t *job_step_kill_msg = msg->data;

	log_flag(STEPS, "Processing RPC details: REQUEST_CANCEL_JOB_STEP %ps flags=0x%x",
		 &job_step_kill_msg->step_id, job_step_kill_msg->flags);
	_throttle_start(&active_rpc_cnt);

	error_code = kill_job_step(job_step_kill_msg, msg->auth_uid);

	_throttle_fini(&active_rpc_cnt);

	slurm_send_rc_msg(msg, error_code);
}

/* _slurm_rpc_complete_job_allocation - process RPC to note the
 *	completion of a job allocation */
static void _slurm_rpc_complete_job_allocation(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	complete_job_allocation_msg_t *comp_msg = msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	job_record_t *job_ptr;

	/* init */
	START_TIMER;
	debug3("Processing RPC details: REQUEST_COMPLETE_JOB_ALLOCATION for %pI rc=%d",
	       &comp_msg->step_id, comp_msg->job_rc);

	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	job_ptr = find_job(&comp_msg->step_id);
	log_flag(TRACE_JOBS, "%s: enter %pJ", __func__, job_ptr);

	/* Mark job and/or job step complete */
	error_code = job_complete(&comp_msg->step_id, msg->auth_uid, false,
				  false, comp_msg->job_rc);
	if (error_code) {
		if (error_code == ESLURM_INVALID_JOB_ID) {
			info("%s: %pI error %s",
			     __func__, &comp_msg->step_id,
			     slurm_strerror(error_code));
		} else {
			info("%s: %pJ error %s",
			     __func__, job_ptr, slurm_strerror(error_code));
		}
	} else {
		debug2("%s: %pJ %s", __func__, job_ptr, TIME_STR);
	}

	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);
	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		slurm_send_rc_msg(msg, error_code);
	} else {
		slurmctld_diag_stats.jobs_completed++;
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		(void) schedule_job_save();	/* Has own locking */
		(void) schedule_node_save();	/* Has own locking */
	}

	log_flag(TRACE_JOBS, "%s: return %pJ", __func__, job_ptr);
}

/* _slurm_rpc_complete_prolog - process RPC to note the
 *	completion of a prolog */
static void _slurm_rpc_complete_prolog(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	prolog_complete_msg_t *comp_msg = msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	/* init */
	START_TIMER;
	debug3("Processing RPC details: REQUEST_COMPLETE_PROLOG from %pI",
	       &comp_msg->step_id);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_write_lock);
	error_code = prolog_complete(comp_msg);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(job_write_lock);

	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		info("%s: %pI: %s ",
		     __func__, &comp_msg->step_id, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s: %pI %s", __func__, &comp_msg->step_id, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

/* _slurm_rpc_complete_batch - process RPC from slurmstepd to note the
 *	completion of a batch script */
static void _slurm_rpc_complete_batch_script(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS, i;
	DEF_TIMERS;
	complete_batch_script_msg_t *comp_msg = msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	bool job_requeue = false;
	bool dump_job = false, dump_node = false;
	job_record_t *job_ptr = NULL;
	char *nodes = comp_msg->node_name;

	/* init */
	START_TIMER;
	debug3("Processing RPC details: REQUEST_COMPLETE_BATCH_SCRIPT for %pI",
	       &comp_msg->step_id);

	if (!validate_slurm_user(msg->auth_uid)) {
		error("A non superuser %u tried to complete batch %pI",
		      msg->auth_uid, &comp_msg->step_id);
		/* Only the slurmstepd can complete a batch script */
		END_TIMER2(__func__);
		return;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	job_ptr = find_job(&comp_msg->step_id);

	if (job_ptr && job_ptr->batch_host && comp_msg->node_name &&
	    xstrcmp(job_ptr->batch_host, comp_msg->node_name)) {
		/* This can be the result of the slurmd on the batch_host
		 * failing, but the slurmstepd continuing to run. Then the
		 * batch job is requeued and started on a different node.
		 * The end result is one batch complete RPC from each node. */
		error("Batch completion for %pI sent from wrong node (%s rather than %s). Was the job requeued due to node failure?",
		      &comp_msg->step_id, comp_msg->node_name,
		      job_ptr->batch_host);
		if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	/*
	 * Send batch step info to accounting, only if the job is
	 * still completing.
	 *
	 * When a job is requeued because of node failure, and there is no
	 * epilog, both EPILOG_COMPLETE and COMPLETE_BATCH_SCRIPT_COMPLETE
	 * messages are sent at the same time and received on different
	 * threads. EPILOG_COMPLETE will grab a new db_index for the job. So if
	 * COMPLETE_BATCH_SCRIPT happens after EPILOG_COMPLETE, then adding the
	 * batch step would happen on the new db instance -- which is incorrect.
	 * Rather than try to ensure that COMPLETE_BATCH_SCRIPT happens after
	 * EPILOG_COMPLETE, just throw away the batch step for node failures.
	 *
	 * NOTE: Do not use IS_JOB_PENDING since that doesn't take
	 * into account the COMPLETING FLAG which is valid, but not
	 * always set yet when the step exits normally.
	 */
	if (slurm_with_slurmdbd() && job_ptr &&
	    (job_ptr->job_state != JOB_PENDING)) {
		/* This logic was taken from _slurm_rpc_step_complete() */
		slurm_step_id_t step_id = STEP_ID_FROM_JOB_RECORD(job_ptr);
		step_record_t *step_ptr = NULL;

		step_id.step_id = SLURM_BATCH_SCRIPT;
		if (!(step_ptr = find_step_record(job_ptr, &step_id))) {
			/* Ignore duplicate or late batch complete RPCs */
			debug("%s: Ignoring late or duplicate REQUEST_COMPLETE_BATCH_SCRIPT received for job %pJ",
			      __func__, job_ptr);
			if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
				unlock_slurmctld(job_write_lock);
				_throttle_fini(&active_rpc_cnt);
			}
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			return;
		} else if (step_ptr->step_id.step_id != SLURM_BATCH_SCRIPT) {
			error("%s: %pJ Didn't find batch step, found step %u. This should never happen.",
			      __func__, job_ptr, step_ptr->step_id.step_id);
		} else {
			step_ptr->exit_code = comp_msg->job_rc;
			jobacctinfo_destroy(step_ptr->jobacct);
			step_ptr->jobacct = comp_msg->jobacct;
			comp_msg->jobacct = NULL;
			step_ptr->state |= JOB_COMPLETING;
			jobacct_storage_g_step_complete(acct_db_conn, step_ptr);
			delete_step_record(job_ptr, step_ptr);
		}
	}

	/* do RPC call */
	/* First set node DOWN if fatal error */
	if ((comp_msg->slurm_rc == ESLURMD_STEP_NOTRUNNING) ||
	    (comp_msg->slurm_rc == ESLURM_ALREADY_DONE) ||
	    (comp_msg->slurm_rc == ESLURMD_CREDENTIAL_REVOKED)) {
		/* race condition on job termination, not a real error */
		info("slurmd error running %pI from Node(s)=%s: %s",
		     &comp_msg->step_id, nodes,
		     slurm_strerror(comp_msg->slurm_rc));
		comp_msg->slurm_rc = SLURM_SUCCESS;
	} else if ((comp_msg->slurm_rc == SLURM_COMMUNICATIONS_SEND_ERROR) ||
		   (comp_msg->slurm_rc == ESLURM_USER_ID_MISSING) ||
		   (comp_msg->slurm_rc == ESLURMD_INVALID_ACCT_FREQ) ||
		   (comp_msg->slurm_rc == ESPANK_JOB_FAILURE)) {
		/* Handle non-fatal errors here. All others drain the node. */
		error("Slurmd error running %pI on Node(s)=%s: %s",
		      &comp_msg->step_id, nodes,
		      slurm_strerror(comp_msg->slurm_rc));
	} else if (comp_msg->slurm_rc != SLURM_SUCCESS) {
		error("slurmd error running %pI on Node(s)=%s: %s",
		      &comp_msg->step_id, nodes,
		      slurm_strerror(comp_msg->slurm_rc));
		slurmctld_diag_stats.jobs_failed++;
		if (error_code == SLURM_SUCCESS) {
			error_code = drain_nodes(comp_msg->node_name,
			                         "batch job complete failure",
			                         slurm_conf.slurm_user_id);
			if ((comp_msg->job_rc != SLURM_SUCCESS) && job_ptr &&
			    job_ptr->details && job_ptr->details->requeue)
				job_requeue = true;
			dump_job = true;
			dump_node = true;
		}
	}

	/*
	 * If we've already sent the SIGTERM signal from
	 * _job_check_grace_internal assume the job completed on signal, that's
	 * subjected to a race condition. The job may just complete just before
	 * we deliver the signal.
	 */
	if (job_ptr && (job_ptr->bit_flags & GRACE_PREEMPT) &&
	    job_ptr->details && job_ptr->details->requeue &&
	    (slurm_job_preempt_mode(job_ptr) == PREEMPT_MODE_REQUEUE))
		job_requeue = true;

	/* Mark job allocation complete */
	i = job_complete(&comp_msg->step_id, msg->auth_uid, job_requeue, false,
			 comp_msg->job_rc);
	error_code = MAX(error_code, i);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

	/* this has to be done after the job_complete */

	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		debug2("%s: %pI: %s ",
		       __func__, &comp_msg->step_id,
		       slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s: %pI %s", __func__, &comp_msg->step_id, TIME_STR);
		slurmctld_diag_stats.jobs_completed++;
		dump_job = true;
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

	if (dump_job)
		(void) schedule_job_save();	/* Has own locking */
	if (dump_node)
		(void) schedule_node_save();	/* Has own locking */
}

static void _slurm_rpc_dump_batch_script(slurm_msg_t *msg)
{
	DEF_TIMERS;
	int rc = SLURM_SUCCESS;
	job_record_t *job_ptr;
	buf_t *script;
	job_id_msg_t *job_id_msg = msg->data;
	/* Locks: Read config, job, and node info */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	START_TIMER;
	debug3("Processing RPC details: REQUEST_BATCH_SCRIPT for %pI",
	       &job_id_msg->step_id);
	lock_slurmctld(job_read_lock);

	if ((job_ptr = find_job(&job_id_msg->step_id))) {
		if (!validate_operator(msg->auth_uid) &&
		    (job_ptr->user_id != msg->auth_uid)) {
			rc = ESLURM_USER_ID_MISSING;
		} else {
			script = get_job_script(job_ptr);
			if (!script)
				rc = ESLURM_JOB_SCRIPT_MISSING;
		}
	} else {
		rc = ESLURM_INVALID_JOB_ID;
	}

	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		(void) send_msg_response(msg, RESPONSE_BATCH_SCRIPT, script);
		FREE_NULL_BUFFER(script);
	}
}

static void _step_create_job_lock(bool lock)
{
	static int active_rpc_cnt = 0;
	slurmctld_lock_t job_write_lock = {
		.job = WRITE_LOCK,
		.node = READ_LOCK,
	};

	if (lock) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	} else {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}
}

static void _step_create_job_fail_lock(bool lock)
{
	static int active_rpc_cnt = 0;
	/* Same locks as _slurm_rpc_step_complete */
	slurmctld_lock_t job_write_lock = {
		.job = WRITE_LOCK,
		.node = WRITE_LOCK,
		.fed = READ_LOCK,
	};

	if (lock) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	} else {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}
}

/* _slurm_rpc_job_step_create - process RPC to create/register a job step
 *	with the stepmgr */
static void _slurm_rpc_job_step_create(slurm_msg_t *msg)
{
	if (!step_create_from_msg(msg, -1,
				  ((!(msg->flags & CTLD_QUEUE_PROCESSING)) ?
					   _step_create_job_lock :
					   NULL),
				  ((!(msg->flags & CTLD_QUEUE_PROCESSING)) ?
					   _step_create_job_fail_lock :
					   NULL))) {
		schedule_job_save();	/* Sets own locks */
	}
}

static int _pack_ctld_job_steps(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	pack_step_args_t *args = (pack_step_args_t *) arg;

	if ((args->step_id->job_id != NO_VAL) &&
	    (args->step_id->job_id != job_ptr->job_id) &&
	    (args->step_id->job_id != job_ptr->array_job_id))
		return 0;

	args->valid_job = 1;

	if (((args->show_flags & SHOW_ALL) == 0) && !args->privileged &&
	    (job_ptr->part_ptr) &&
	    part_not_on_list(args->visible_parts, job_ptr->part_ptr))
		return 0;

	if ((slurm_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != args->uid) && !args->privileged) {
		if (slurm_mcs_get_privatedata()) {
			if (mcs_g_check_mcs_label(args->uid,
						  job_ptr->mcs_label, false))
				return 0;
		} else if (!assoc_mgr_is_user_acct_coord(acct_db_conn,
							 args->uid,
							 job_ptr->account,
							 false)) {
			return 0;
		}
	}

	/*
	 * Pack a single requested step, or pack all steps.
	 */
	if (args->step_id->step_id != NO_VAL ) {
		step_record_t *step_ptr = find_step_record(job_ptr,
							   args->step_id);
		if (!step_ptr)
			goto fini;
		pack_ctld_job_step_info(step_ptr, args);
	} else {
		list_for_each(job_ptr->step_list,
			      pack_ctld_job_step_info,
			      args);
	}

fini:
	/*
	 * Only return stepmgr_jobs if looking for a specific job to avoid
	 * querying all stepmgr's for all steps.
	 */
	if ((args->step_id->job_id != NO_VAL) &&
	    (job_ptr->bit_flags & STEPMGR_ENABLED) &&
	    IS_JOB_RUNNING(job_ptr)) {
		stepmgr_job_info_t *sji = xmalloc(sizeof(*sji));
		if (!args->stepmgr_jobs)
			args->stepmgr_jobs = list_create(NULL);
		sji->step_id = STEP_ID_FROM_JOB_RECORD(job_ptr);
		sji->stepmgr = job_ptr->batch_host;
		list_append(args->stepmgr_jobs, sji);
	}

	return 0;
}

/* _slurm_rpc_job_step_get_info - process request for job step info */
static void _slurm_rpc_job_step_get_info(slurm_msg_t *msg)
{
	DEF_TIMERS;
	buf_t *buffer = NULL;
	int error_code = SLURM_SUCCESS;
	job_step_info_request_msg_t *request = msg->data;
	/* Locks: Read config, job, part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);

	if ((request->last_update - 1) >= last_job_update) {
		unlock_slurmctld(job_read_lock);
		log_flag(STEPS, "%s: no change", __func__);
		error_code = SLURM_NO_CHANGE_IN_DATA;
	} else {
		bool privileged = validate_operator(msg->auth_uid);
		bool skip_visible_parts =
			(request->show_flags & SHOW_ALL) || privileged;
		pack_step_args_t args = {0};

		buffer = init_buf(BUF_SIZE);

		args.step_id = &request->step_id,
		args.show_flags = request->show_flags,
		args.uid = msg->auth_uid,
		args.steps_packed = 0,
		args.buffer = buffer,
		args.privileged = privileged,
		args.proto_version = msg->protocol_version,
		args.valid_job = false,
		args.visible_parts = build_visible_parts(msg->auth_uid,
							 skip_visible_parts),
		args.job_step_list = job_list,
		args.pack_job_step_list_func = _pack_ctld_job_steps,

		error_code = pack_job_step_info_response_msg(&args);

		unlock_slurmctld(job_read_lock);
		END_TIMER2(__func__);
		if (error_code) {
			/* job_id:step_id not found or otherwise *\
			\* error message is printed elsewhere    */
			log_flag(STEPS, "%s: %s",
				 __func__, slurm_strerror(error_code));
		}
	}

	if (error_code)
		slurm_send_rc_msg(msg, error_code);
	else {
		(void) send_msg_response(msg, RESPONSE_JOB_STEP_INFO, buffer);
	}

	FREE_NULL_BUFFER(buffer);
}

/* _slurm_rpc_job_will_run - process RPC to determine if job with given
 *	configuration can be initiated */
static void _slurm_rpc_job_will_run(slurm_msg_t *msg)
{
	/* init */
	DEF_TIMERS;
	int error_code = SLURM_SUCCESS;
	job_record_t *job_ptr = NULL;
	job_desc_msg_t *job_desc_msg = msg->data;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition, read fed*/
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	will_run_response_msg_t *resp = NULL;
	char *err_msg = NULL, *job_submit_user_msg = NULL;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		goto send_reply;
	}

	START_TIMER;
	if ((error_code = _valid_id("REQUEST_JOB_WILL_RUN", job_desc_msg,
				    msg->auth_uid, msg->auth_gid,
				    msg->protocol_version)))
		goto send_reply;

	_set_hostname(msg, &job_desc_msg->alloc_node);
	_set_identity(msg, &job_desc_msg->id);

	if ((job_desc_msg->alloc_node == NULL)
	    ||  (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_JOB_WILL_RUN lacks alloc_node from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		lock_slurmctld(job_read_lock);
		job_desc_msg->het_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,
						     msg->auth_uid, &err_msg);
		unlock_slurmctld(job_read_lock);
	}

	if (err_msg)
		job_submit_user_msg = xstrdup(err_msg);

	if (msg->address.ss_family != AF_UNSPEC) {
		job_desc_msg->resp_host = xmalloc(INET6_ADDRSTRLEN);
		slurm_get_ip_str(&msg->address, job_desc_msg->resp_host,
				 INET6_ADDRSTRLEN);
		dump_job_desc(job_desc_msg);
		if (error_code == SLURM_SUCCESS) {
			lock_slurmctld(job_write_lock);
			if (job_desc_msg->step_id.job_id == NO_VAL) {
				job_desc_msg->het_job_offset = NO_VAL;
				error_code = job_allocate(job_desc_msg, false,
							  true, &resp, true,
							  msg->auth_uid, false,
							  &job_ptr,
							  &err_msg,
							  msg->protocol_version);
			} else { /* existing job test */
				job_ptr = find_job(&job_desc_msg->step_id);
				error_code = job_start_data(job_ptr, &resp);
			}
			unlock_slurmctld(job_write_lock);
			END_TIMER2(__func__);
		}
	} else {
		error_code = SLURM_UNKNOWN_FORWARD_ADDR;
	}

send_reply:

	/* return result */
	if (error_code) {
		debug2("%s: %s", __func__, slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else if (resp) {
		resp->job_submit_user_msg = job_submit_user_msg;
		job_submit_user_msg = NULL;
		(void) send_msg_response(msg, RESPONSE_JOB_WILL_RUN, resp);
		slurm_free_will_run_response_msg(resp);
		debug2("%s success %s", __func__, TIME_STR);
	} else {
		debug2("%s success %s", __func__, TIME_STR);
		if (job_desc_msg->step_id.job_id == NO_VAL)
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

static bool _node_has_feature(node_record_t *node_ptr, char *feature)
{
	node_feature_t *node_feature;

	if ((node_feature = list_find_first(active_feature_list,
					    list_find_feature, feature))) {
		if (bit_test(node_feature->node_bitmap, node_ptr->index))
		    return true;
	}

	return false;
}

#define FUTURE_MAP_FAILED -1 /* failed to map registration to future node */
#define FUTURE_MAP_SUCCESS 0 /* mapped the registration to a future node */
#define FUTURE_MAP_EXISTING 1 /* found an existing mapped node */

/*
 * Find available future node to associate slurmd with.
 *
 * Sets reg_msg->node_name to found node_name so subsequent calls to
 * find the node work.
 *
 * return: FUTURE_MAP_*
 */
static int _find_avail_future_node(slurm_msg_t *msg)
{
	node_record_t *node_ptr;
	slurm_node_registration_status_msg_t *reg_msg = msg->data;
	int rc = FUTURE_MAP_FAILED;

	node_ptr = find_node_record2(reg_msg->hostname);
	if (node_ptr == NULL) {
		int i;
		time_t now;

		debug2("finding available dynamic future node for %s/%s",
		       reg_msg->node_name, reg_msg->hostname);

		for (i = 0; (node_ptr = next_node(&i)); i++) {
			slurm_addr_t addr;
			char *comm_name = NULL;

			if (!IS_NODE_FUTURE(node_ptr))
			    continue;

			if (reg_msg->dynamic_feature &&
			    !_node_has_feature(
				node_ptr,reg_msg->dynamic_feature))
				continue;
			else if ((node_ptr->cpus != reg_msg->cpus) ||
				 (node_ptr->boards != reg_msg->boards) ||
				 (node_ptr->tot_sockets != reg_msg->sockets) ||
				 (node_ptr->cores != reg_msg->cores) ||
				 (node_ptr->threads != reg_msg->threads))
				continue;

			/* Get IP of slurmd */
			if (msg->address.ss_family != AF_UNSPEC) {
				comm_name = xmalloc(INET6_ADDRSTRLEN);
				slurm_get_ip_str(&addr, comm_name,
						 INET6_ADDRSTRLEN);
			}

			set_node_comm_name(node_ptr, comm_name,
					   reg_msg->hostname);
			now = time(NULL);
			node_ptr->node_state = NODE_STATE_IDLE;
			node_ptr->node_state |= NODE_STATE_DYNAMIC_FUTURE;
			node_ptr->last_response = now;
			node_ptr->last_busy = now;

			/*
			 * When 24.11 is no longer supported, remove this if
			 * block.
			 */
			if (msg->protocol_version <=
			    SLURM_24_11_PROTOCOL_VERSION) {
				/*
				 * As we don't validate the node specs until the
				 * 2nd registration RPC, and slurmd only sends
				 * instance-like attributes in the 1st
				 * registration RPC of its lifetime, we need to
				 * store these values here.
				 */
				if (reg_msg->instance_id) {
					xfree(node_ptr->instance_id);
					if (reg_msg->instance_id[0])
						node_ptr->instance_id =
							xstrdup(reg_msg->instance_id);
				}
				if (reg_msg->instance_type) {
					xfree(node_ptr->instance_type);
					if (reg_msg->instance_type[0])
						node_ptr->instance_type =
							xstrdup(reg_msg->instance_type);
				}
			}

			bit_clear(future_node_bitmap, node_ptr->index);
			xfree(comm_name);

			clusteracct_storage_g_node_up(acct_db_conn, node_ptr,
						      now);

			rc = FUTURE_MAP_SUCCESS;
			break;
		}
	} else {
		debug2("found existing node %s/%s for dynamic future node registration",
		       reg_msg->node_name, reg_msg->hostname);
		rc = FUTURE_MAP_EXISTING;
	}

	if (node_ptr && (rc != FUTURE_MAP_FAILED)) {
		debug2("dynamic future node %s/%s/%s assigned to node %s",
		       reg_msg->node_name, node_ptr->node_hostname,
		       node_ptr->comm_name, node_ptr->name);
		/*
		 * We always need to send the hostname back to the slurmd. In
		 * case the slurmd already registered and we found the node_ptr
		 * by the node_hostname.
		 */
		xfree(reg_msg->node_name);
		reg_msg->node_name = xstrdup(node_ptr->name);
	} else if (rc == FUTURE_MAP_FAILED) {
		error("Failed to map %s/%s to an available future node",
		       reg_msg->node_name, reg_msg->hostname);
	}

	return rc;
}

static void _slurm_post_rpc_node_registration()
{
	if (do_post_rpc_node_registration)
		clusteracct_storage_g_cluster_tres(acct_db_conn, NULL, NULL, 0,
						   SLURM_PROTOCOL_VERSION);
	do_post_rpc_node_registration = false;
}

/* _slurm_rpc_node_registration - process RPC to determine if a node's
 *	actual configuration satisfies the configured specification */
static void _slurm_rpc_node_registration(slurm_msg_t *msg)
{
	/* init */
	DEF_TIMERS;
	int error_code = SLURM_SUCCESS;
	bool newly_up = false;
	bool already_registered = false;
	slurm_node_registration_status_msg_t *node_reg_stat_msg = msg->data;
	slurmctld_lock_t job_write_lock = {
		.conf = READ_LOCK,
		.job = WRITE_LOCK,
		.node = WRITE_LOCK,
		.part = WRITE_LOCK,
		.fed = READ_LOCK,
	};

	START_TIMER;
	if (!validate_slurm_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, NODE_REGISTER RPC from uid=%u",
		      msg->auth_uid);
	}

	if (msg->protocol_version != SLURM_PROTOCOL_VERSION)
		info("Node %s appears to have a different version "
		     "of Slurm than ours.  Please update at your earliest "
		     "convenience.", node_reg_stat_msg->node_name);

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		if (!(slurm_conf.debug_flags & DEBUG_FLAG_NO_CONF_HASH) &&
		    (node_reg_stat_msg->hash_val != NO_VAL) &&
		    (node_reg_stat_msg->hash_val != slurm_conf.hash_val)) {
			error("Node %s appears to have a different slurm.conf "
			      "than the slurmctld.  This could cause issues "
			      "with communication and functionality.  "
			      "Please review both files and make sure they "
			      "are the same.  If this is expected ignore, and "
			      "set DebugFlags=NO_CONF_HASH in your slurm.conf.",
			      node_reg_stat_msg->node_name);
		}
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			lock_slurmctld(job_write_lock);

		if (node_reg_stat_msg->dynamic_type &&
		    (node_reg_stat_msg->flags & SLURMD_REG_FLAG_RESP)) {

			if (node_reg_stat_msg->dynamic_type == DYN_NODE_FUTURE) {
				int rc;
				/*
				 * dynamic future nodes doesn't know what node
				 * it's mapped to to be able to load all configs
				 * in. slurmctld will tell the slurmd what node
				 * it's mapped to and then the slurmd will then
				 * load in configuration based off of the mapped
				 * name and send another registration.
				 *
				 * Subsequent slurmd registrations will have the
				 * mapped node_name.
				 */
				rc = _find_avail_future_node(msg);

				/*
				 * FUTURE_MAP_SUCCESS: assigned registration to
				 * a new nodename and the slurmd just needs the
				 * mapped name so it can register again.
				 *
				 * FUTURE_MAP_FAILED: failed to find a future
				 * not do map to so, just skip validating the
				 * registration and return to the slurmd.
				 *
				 * FUTURE_MAP_EXISTING: the node is already
				 * mapped and we need to validate the
				 * registration.
				 */
				if (rc != FUTURE_MAP_EXISTING) {
					if (!(msg->flags & CTLD_QUEUE_PROCESSING))
						unlock_slurmctld(job_write_lock);

					if (rc == FUTURE_MAP_FAILED)
						error_code = ESLURM_INVALID_NODE_NAME;

					goto send_resp;
				}
			} else if (find_node_record2(
					node_reg_stat_msg->node_name)) {
				already_registered = true;
			} else {
				(void) create_dynamic_reg_node(msg);
			}
		}

		validate_jobs_on_node(msg);
		error_code = validate_node_specs(msg, &newly_up);

		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_write_lock);
		END_TIMER2(__func__);
		if (newly_up) {
			queue_job_scheduler();
		}
	}


send_resp:

	/* return result */
	if (error_code) {
		error("%s node=%s: %s",
		      __func__, node_reg_stat_msg->node_name,
		      slurm_strerror(error_code));
		/*
		 * Notify slurmd that we got the registration even if we
		 * consider it to be invalid to avoid having slurmd try to
		 * register again continuously.
		 */
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	} else {
		debug2("%s complete for %s %s",
		       __func__, node_reg_stat_msg->node_name, TIME_STR);
		/* If the slurmd is requesting a response send it */
		if (node_reg_stat_msg->flags & SLURMD_REG_FLAG_RESP) {
			slurm_node_reg_resp_msg_t tmp_resp;
			memset(&tmp_resp, 0, sizeof(tmp_resp));

			/*
			 * Don't add the assoc_mgr_tres_list here as it could
			 * get freed later if you do.  The pack functions grab
			 * it for us if it isn't here.
			 */
			//tmp_resp.tres_list = assoc_mgr_tres_list;

			if (node_reg_stat_msg->dynamic_type)
				tmp_resp.node_name =
					node_reg_stat_msg->node_name;

			(void) send_msg_response(msg,
						 RESPONSE_NODE_REGISTRATION,
						 &tmp_resp);
		} else
			slurm_send_rc_msg(msg, SLURM_SUCCESS);

		if (!already_registered &&
		    (node_reg_stat_msg->dynamic_type == DYN_NODE_NORM)) {
			if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
				/* Must be called outside of locks */
				clusteracct_storage_g_cluster_tres(
					acct_db_conn, NULL, NULL, 0,
					SLURM_PROTOCOL_VERSION);
			} else {
				do_post_rpc_node_registration = true;
			}
		}
	}
}

/* _slurm_rpc_job_alloc_info - process RPC to get details on existing job */
static void _slurm_rpc_job_alloc_info(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	job_record_t *job_ptr;
	DEF_TIMERS;
	job_alloc_info_msg_t *job_info_msg = msg->data;
	resource_allocation_response_msg_t *job_info_resp_msg;
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);
	error_code =
		job_alloc_info(msg->auth_uid, &job_info_msg->step_id, &job_ptr);
	END_TIMER2(__func__);

	/* return result */
	if (error_code || (job_ptr == NULL) || (job_ptr->job_resrcs == NULL)) {
		unlock_slurmctld(job_read_lock);
		debug2("%s: %pI, uid=%u: %s",
		       __func__, &job_info_msg->step_id, msg->auth_uid,
		      slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug("%s: %pI NodeList=%s %s",
		      __func__, &job_info_msg->step_id, job_ptr->nodes,
		      TIME_STR);

		job_info_resp_msg = build_job_info_resp(job_ptr);
		set_remote_working_response(job_info_resp_msg, job_ptr,
					    job_info_msg->req_cluster);
		unlock_slurmctld(job_read_lock);

		(void) send_msg_response(msg, RESPONSE_JOB_ALLOCATION_INFO,
					 job_info_resp_msg);

		/* NULL out msg->working_cluster_rec because it's pointing to
		 * the global memory */
		job_info_resp_msg->working_cluster_rec = NULL;
		slurm_free_resource_allocation_response_msg(job_info_resp_msg);
	}
}

static void _het_job_alloc_list_del(void *x)
{
	resource_allocation_response_msg_t *job_info_resp_msg;

	job_info_resp_msg = (resource_allocation_response_msg_t *) x;
	/* NULL out msg->working_cluster_rec because it's pointing to
	 * the global memory */
	job_info_resp_msg->working_cluster_rec = NULL;
	slurm_free_resource_allocation_response_msg(job_info_resp_msg);
}

/*
 * _slurm_rpc_het_job_alloc_info - process RPC to get details on existing
 *				       hetjob.
 */
static void _slurm_rpc_het_job_alloc_info(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	job_record_t *job_ptr, *het_job;
	list_itr_t *iter;
	void *working_cluster_rec = NULL;
	list_t *resp = NULL;
	DEF_TIMERS;
	job_alloc_info_msg_t *job_info_msg = msg->data;
	resource_allocation_response_msg_t *job_info_resp_msg;
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_read_lock);
	error_code =
		job_alloc_info(msg->auth_uid, &job_info_msg->step_id, &job_ptr);
	END_TIMER2(__func__);

	/* return result */
	if ((error_code == SLURM_SUCCESS) && job_ptr &&
	    (job_ptr->het_job_id && !job_ptr->het_job_list))
		error_code = ESLURM_NOT_HET_JOB_LEADER;
	if (error_code || (job_ptr == NULL) || (job_ptr->job_resrcs == NULL)) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_read_lock);
		debug2("%s: %pI, uid=%u: %s",
		       __func__, &job_info_msg->step_id, msg->auth_uid,
		       slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	debug2("%s: %pI NodeList=%s %s", __func__,
	       &job_info_msg->step_id, job_ptr->nodes, TIME_STR);

	if (!job_ptr->het_job_list) {
		resp = list_create(_het_job_alloc_list_del);
		job_info_resp_msg = build_job_info_resp(job_ptr);
		set_remote_working_response(job_info_resp_msg, job_ptr,
					    job_info_msg->req_cluster);
		list_append(resp, job_info_resp_msg);
	} else {
		resp = list_create(_het_job_alloc_list_del);
		iter = list_iterator_create(job_ptr->het_job_list);
		while ((het_job = list_next(iter))) {
			if (job_ptr->het_job_id != het_job->het_job_id) {
				error("%s: Bad het_job_list for %pJ",
				      __func__, job_ptr);
				continue;
			}
			if (het_job->job_id != job_info_msg->step_id.job_id)
				(void) job_alloc_info_ptr(msg->auth_uid,
							  het_job);
			job_info_resp_msg = build_job_info_resp(het_job);
			if (working_cluster_rec) {
				job_info_resp_msg->working_cluster_rec =
					working_cluster_rec;
			} else {
				set_remote_working_response(job_info_resp_msg,
						het_job,
						job_info_msg->req_cluster);
				working_cluster_rec =
					job_info_resp_msg->working_cluster_rec;
			}
			list_append(resp, job_info_resp_msg);
		}
		list_iterator_destroy(iter);
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(job_read_lock);

	(void) send_msg_response(msg, RESPONSE_HET_JOB_ALLOCATION, resp);
	FREE_NULL_LIST(resp);
}

/* _slurm_rpc_job_sbcast_cred - process RPC to get details on existing job
 *	plus sbcast credential */
static void _slurm_rpc_job_sbcast_cred(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	job_record_t *job_ptr = NULL;
	DEF_TIMERS;
	step_alloc_info_msg_t *job_info_msg = msg->data;
	job_sbcast_cred_msg_t *job_info_resp_msg = NULL;
	char job_id_str[64];
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);
	if (job_info_msg->het_job_offset == NO_VAL) {
		error_code = job_alloc_info(msg->auth_uid,
					    &job_info_msg->step_id, &job_ptr);
	} else {
		job_ptr = find_het_job_record(job_info_msg->step_id.job_id,
					      job_info_msg->het_job_offset);
		if (job_ptr) {
			job_info_msg->step_id =
				STEP_ID_FROM_JOB_RECORD(job_ptr);
			error_code = job_alloc_info(msg->auth_uid,
						    &job_info_msg->step_id,
						    &job_ptr);
		} else {
			error_code = ESLURM_INVALID_JOB_ID;
		}
	}

	if (error_code)
		goto error;

	if (!job_ptr) {
		error_code = ESLURM_INVALID_JOB_ID;
		goto error;
	}

	if (job_ptr->bit_flags & EXTERNAL_JOB) {
		error("%s: job step creation disabled for external jobs",
		      __func__);
		slurm_send_rc_msg(msg, ESLURM_NOT_SUPPORTED);
		unlock_slurmctld(job_read_lock);
		return;
	}

	if (job_ptr->bit_flags & STEPMGR_ENABLED) {
		slurm_send_reroute_msg(msg, NULL, job_ptr->batch_host);
		unlock_slurmctld(job_read_lock);
		return;
	}

	if (!validate_operator(msg->auth_uid) &&
	    (job_ptr->user_id != msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		goto error;
	}

	error_code = stepmgr_get_job_sbcast_cred_msg(job_ptr,
						     &job_info_msg->step_id,
						     msg->protocol_version,
						     &job_info_resp_msg);
	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (error_code)
		goto error;

	info("%s: %s NodeList=%s - %s",
	     __func__,
	     slurm_get_selected_step_id(job_id_str, sizeof(job_id_str),
					job_info_msg),
	     job_info_resp_msg->node_list,
	     TIME_STR);

	(void) send_msg_response(msg, RESPONSE_JOB_SBCAST_CRED,
				 job_info_resp_msg);

	slurm_free_sbcast_cred_msg(job_info_resp_msg);

	return;

error:
	unlock_slurmctld(job_read_lock);

	debug2("%s: JobId=%s, uid=%u: %s",
	       __func__,
	       slurm_get_selected_step_id(job_id_str,
					  sizeof(job_id_str),
					  job_info_msg),
	       msg->auth_uid,
	       slurm_strerror(error_code));

	slurm_send_rc_msg(msg, error_code);
}

static void _slurm_rpc_sbcast_cred_no_job(slurm_msg_t *msg)
{
	job_sbcast_cred_msg_t *cred_resp_msg = NULL;
	sbcast_cred_req_msg_t *cred_req_msg = msg->data;
	sbcast_cred_arg_t sbcast_arg = { { 0 } };
	sbcast_cred_t *sbcast_cred;
	hostlist_t *req_node_list;
	char *node_name;
	bool node_exists = false;
	int rc;

	DEF_TIMERS;
	START_TIMER;

	if (!validate_slurm_user(msg->auth_uid)) {
		error("%s: sbcast --no-allocation/-Z credential requested from uid '%u' which is not root/SlurmUser",
		      __func__, msg->auth_uid);
		rc = ESLURM_USER_ID_MISSING;
		goto fail;
	}

	req_node_list = hostlist_create(cred_req_msg->node_list);
	while ((node_name = hostlist_shift(req_node_list))) {
		node_exists = find_node_record(node_name);

		if (!node_exists) {
			debug("%s: sbcast --nodelist contains at least one invalid node '%s'",
			      __func__, node_name);
			free(node_name);
			break;
		}
		free(node_name);
	}
	FREE_NULL_HOSTLIST(req_node_list);

	if (!node_exists) {
		(void) slurm_send_rc_msg(msg, ESLURM_INVALID_NODE_NAME);
		return;
	}

	sbcast_arg.nodes = cred_req_msg->node_list;
	sbcast_arg.expiration = time(NULL) + HOUR_SECONDS;

	if (!(sbcast_cred = create_sbcast_cred(&sbcast_arg, msg->auth_uid,
					       msg->auth_gid,
					       msg->protocol_version))) {
		error("%s: Could not create sbcast cred for --no-allocate/-Z request",
		      __func__);
		rc = SLURM_ERROR;
		goto fail;
	}
	END_TIMER2(__func__);

	cred_resp_msg = xmalloc(sizeof(*cred_resp_msg));
	cred_resp_msg->node_list = xstrdup(cred_req_msg->node_list);
	cred_resp_msg->sbcast_cred = sbcast_cred;

	(void) send_msg_response(msg, RESPONSE_JOB_SBCAST_CRED, cred_resp_msg);

	slurm_free_sbcast_cred_msg(cred_resp_msg);

	return;

fail:
	END_TIMER2(__func__);
	(void) slurm_send_rc_msg(msg, rc);
}

/* _slurm_rpc_ping - process ping RPC */
static void _slurm_rpc_ping(slurm_msg_t *msg)
{
	/* We could authenticate here, if desired */

	/* return result */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_config_request(slurm_msg_t *msg)
{
	config_request_msg_t *req = msg->data;
	DEF_TIMERS;

	START_TIMER;
	if (!running_configless) {
		error("%s: Rejected request as configless is disabled",
		      __func__);
		slurm_send_rc_msg(msg, ESLURM_CONFIGLESS_DISABLED);
		return;
	}

	if ((req->flags & CONFIG_REQUEST_SLURMD) &&
	    !validate_slurm_user(msg->auth_uid)) {
		error("%s: Rejected request for slurmd configs by uid=%u",
		      __func__, msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}
	END_TIMER2(__func__);

	slurm_rwlock_rdlock(&configless_lock);
	{
		void *data = config_for_clients;
		if (req->flags & CONFIG_REQUEST_SLURMD)
			data = config_for_slurmd;
		(void) send_msg_response(msg, RESPONSE_CONFIG, data);
	}
	slurm_rwlock_unlock(&configless_lock);

	if (req->flags & CONFIG_REQUEST_SACKD)
		sackd_mgr_add_node(msg, req->port);
}

/* _slurm_rpc_reconfigure_controller - process RPC to re-initialize
 *	slurmctld from configuration file
 * Anything you add to this function must be added to the
 * slurm_reconfigure function inside controller.c try
 * to keep these in sync.
 */
static void _slurm_rpc_reconfigure_controller(slurm_msg_t *msg)
{
	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, RECONFIGURE RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		conn_g_destroy(msg->conn, true);
		msg->conn = NULL;
		FREE_NULL_MSG(msg);
		return;
	} else
		info("Processing Reconfiguration Request");

	reconfigure_slurm(msg);
}

/* _slurm_rpc_takeover - process takeover RPC */
static void _slurm_rpc_takeover(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;

	/* We could authenticate here, if desired */
	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, TAKEOVER RPC from uid=%u",
		      msg->auth_uid);
		error_code = ESLURM_USER_ID_MISSING;
	} else {
		/* takeover is not possible in controller mode */
		/* return success */
		info("Performing RPC: REQUEST_TAKEOVER : "
		     "already in controller mode - skipping");
	}

	slurm_send_rc_msg(msg, error_code);

}

static void _slurm_rpc_request_control(slurm_msg_t *msg)
{
	time_t now = time(NULL);
	struct timespec ts = {0, 0};

	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, REQUEST_CONTROL RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	info("Performing RPC: REQUEST_CONTROL");
	slurm_mutex_lock(&slurmctld_config.backup_finish_lock);
	/* resume backup mode */
	slurmctld_config.resume_backup = true;

	/* do RPC call */
	if (slurmctld_config.shutdown_time) {
		debug2("REQUEST_CONTROL RPC issued when already in progress");
	} else {
		/* signal clean-up */
		pthread_kill(pthread_self(), SIGTERM);
	}

	/* save_all_state();	performed by _slurmctld_background */

	/*
	 * Wait for the backup to dump state and finish up everything.
	 * This should happen in _slurmctld_background and then release
	 * once we know for sure we are in backup mode in run_backup().
	 * Here we will wait CONTROL_TIMEOUT - 1 before we reply.
	 */
	ts.tv_sec = now + CONTROL_TIMEOUT - 1;

	slurm_cond_timedwait(&slurmctld_config.backup_finish_cond,
			     &slurmctld_config.backup_finish_lock, &ts);
	slurm_mutex_unlock(&slurmctld_config.backup_finish_lock);

	/*
	 * jobcomp/elasticsearch saves/loads the state to/from file
	 * elasticsearch_state. Since the jobcomp API isn't designed with
	 * save/load state operations, the jobcomp/elasticsearch _save_state()
	 * is highly coupled to its fini() function. This state doesn't follow
	 * the same execution path as the rest of Slurm states, where in
	 * save_all_sate() they are all independently scheduled. So we save
	 * it manually here.
	 */
	jobcomp_g_fini();

	if (slurmctld_config.resume_backup)
		error("%s: REQUEST_CONTROL reply but backup not completely done relinquishing control.  Old state possible", __func__);

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

/* _slurm_rpc_shutdown_controller - process RPC to shutdown slurmctld */
static void _slurm_rpc_shutdown_controller(slurm_msg_t *msg)
{
	shutdown_msg_t *shutdown_msg = msg->data;

	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, SHUTDOWN RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	info("Performing RPC: REQUEST_SHUTDOWN");

	if (slurmctld_config.shutdown_time)
		debug2("shutdown RPC issued when already in progress");
	else {
		if (shutdown_msg->options == SLURMCTLD_SHUTDOWN_ALL) {
			slurmctld_lock_t node_read_lock = { .node = READ_LOCK };
			lock_slurmctld(node_read_lock);
			msg_to_slurmd(REQUEST_SHUTDOWN);
			unlock_slurmctld(node_read_lock);
		}
		/* signal clean-up */
		pthread_kill(pthread_self(), SIGTERM);
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static int _foreach_step_match_containerid(void *x, void *arg)
{
	find_job_by_container_id_args_t *args = arg;
	step_record_t *step_ptr = x;
	slurm_step_id_t *step_id;

	if (xstrcmp(args->id, step_ptr->container_id))
		return SLURM_SUCCESS;

	step_id = xmalloc(sizeof(*step_id));
	*step_id = step_ptr->step_id;

	list_append(args->step_list, step_id);

	return SLURM_SUCCESS;
}

static int _foreach_job_filter_steps(void *x, void *arg)
{
	find_job_by_container_id_args_t *args = arg;
	job_record_t *job_ptr = x;

	if ((slurm_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != args->request_uid) &&
	    !validate_operator(args->request_uid)) {
		if (slurm_mcs_get_privatedata()) {
			if (mcs_g_check_mcs_label(args->request_uid,
						  job_ptr->mcs_label, false))
				return SLURM_SUCCESS;
		} else if (!assoc_mgr_is_user_acct_coord(acct_db_conn,
							 args->request_uid,
							 job_ptr->account,
							 false)) {
			return SLURM_SUCCESS;
		}
	}

	if ((args->uid != SLURM_AUTH_NOBODY) &&
	    (args->uid != job_ptr->user_id)) {
		/* skipping per non-matching user */
		return SLURM_SUCCESS;
	}

	/* walk steps for matching container_id */
	if (list_for_each_ro(job_ptr->step_list,
			     _foreach_step_match_containerid,
			     args) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static void _find_stepids_by_container_id(uid_t request_uid, uid_t uid,
					  const char *id, list_t **step_list)
{
	slurmctld_lock_t job_read_lock =
		{ .conf = READ_LOCK, .job = READ_LOCK };
	find_job_by_container_id_args_t args =
		{ .request_uid = request_uid, .uid = uid, .id = id };
	DEF_TIMERS;

	xassert(id && id[0]);

	if (!*step_list)
		*step_list = list_create((ListDelF) slurm_free_step_id);
	args.step_list = *step_list;

	START_TIMER;
	lock_slurmctld(job_read_lock);
	list_for_each_ro(job_list, _foreach_job_filter_steps, &args);
	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);
}

static void _slurm_rpc_step_by_container_id(slurm_msg_t *msg)
{
	container_id_request_msg_t *req = msg->data;
	container_id_response_msg_t resp = {0};
	int rc = SLURM_UNEXPECTED_MSG_ERROR;

	log_flag(PROTOCOL, "%s: got REQUEST_STEP_BY_CONTAINER_ID from %s auth_uid=%u flags=0x%x uid=%u container_id=%s",
		 __func__, (msg->auth_ids_set ? "validated" : "suspect"),
		 msg->auth_uid, req->show_flags, req->uid, req->container_id);

	if (!msg->auth_ids_set) {
		/* this should never happen? */
		rc = ESLURM_AUTH_CRED_INVALID;
	} else if (!req->container_id || !req->container_id[0]) {
		rc = ESLURM_INVALID_CONTAINER_ID;
	} else {
		if (req->container_id && req->container_id[0])
			_find_stepids_by_container_id(msg->auth_uid, req->uid,
						      req->container_id,
						      &resp.steps);

		(void) send_msg_response(msg, RESPONSE_STEP_BY_CONTAINER_ID,
					 &resp);
		return;
	}

	slurm_send_rc_msg(msg, rc);
}

/* _slurm_rpc_step_complete - process step completion RPC to note the
 *      completion of a job step on at least some nodes.
 *	If the job step is complete, it may
 *	represent the termination of an entire job step */
static void _slurm_rpc_step_complete(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int rc, rem;
	uint32_t step_rc;
	DEF_TIMERS;
	step_complete_msg_t *req = msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };

	/* init */
	START_TIMER;
	log_flag(STEPS, "Processing RPC details: REQUEST_STEP_COMPLETE for %ps nodes %u-%u rc=%u(%s)",
		 &req->step_id, req->range_first, req->range_last,
		 req->step_rc, slurm_strerror(req->step_rc));

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	rc = step_partial_comp(req, msg->auth_uid, true, &rem, &step_rc);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

	END_TIMER2(__func__);

	log_flag(STEPS, "%s: %ps rc:%s %s",
		 __func__, &req->step_id, slurm_strerror(rc), TIME_STR);

	/* return result */
	(void) slurm_send_rc_msg(msg, rc);

	if (rc == SLURM_SUCCESS)
		(void) schedule_job_save();	/* Has own locking */
}

/* _slurm_rpc_step_layout - return the step layout structure for
 *      a job step, if it currently exists
 */
static void _slurm_rpc_step_layout(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	slurm_step_id_t *req = msg->data;
	slurm_step_layout_t *step_layout = NULL;
	/* Locks: Read config job, write node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	job_record_t *job_ptr = NULL;

	START_TIMER;
	lock_slurmctld(job_read_lock);
	error_code = job_alloc_info(msg->auth_uid, req, &job_ptr);
	END_TIMER2(__func__);
	/* return result */
	if (error_code || (job_ptr == NULL)) {
		unlock_slurmctld(job_read_lock);
		if (error_code == ESLURM_ACCESS_DENIED) {
			error("Security violation, REQUEST_STEP_LAYOUT for JobId=%u from uid=%u",
			      req->job_id, msg->auth_uid);
		} else {
			log_flag(STEPS, "%s: JobId=%u, uid=%u: %s",
				 __func__, req->job_id, msg->auth_uid,
				 slurm_strerror(error_code));
		}
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	if (job_ptr->bit_flags & STEPMGR_ENABLED) {
		slurm_send_reroute_msg(msg, NULL, job_ptr->batch_host);
		unlock_slurmctld(job_read_lock);
		return;
	}

	error_code = stepmgr_get_step_layouts(job_ptr, req, &step_layout);
	unlock_slurmctld(job_read_lock);

	if (error_code) {
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	(void) send_msg_response(msg, RESPONSE_STEP_LAYOUT, step_layout);
	slurm_step_layout_destroy(step_layout);
}

/* _slurm_rpc_step_update - update a job step
 */
static void _slurm_rpc_step_update(slurm_msg_t *msg)
{
	DEF_TIMERS;
	job_record_t *job_ptr;
	step_update_request_msg_t *req = msg->data;
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	int rc;

	START_TIMER;
	lock_slurmctld(job_write_lock);

	if (!(job_ptr = find_job(&req->step_id))) {
		error("%s: invalid %pI", __func__, &req->step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto fail;
	}

	if ((job_ptr->user_id != msg->auth_uid) &&
	    !validate_operator(msg->auth_uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, msg->auth_uid,
					  job_ptr->account, false)) {
		error("Security violation, STEP_UPDATE RPC from uid %u",
		      msg->auth_uid);
		rc = ESLURM_USER_ID_MISSING;
		goto fail;
	}

	rc = update_step(req, msg->auth_uid);

fail:
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

/* _slurm_rpc_submit_batch_job - process RPC to submit a batch job */
static void _slurm_rpc_submit_batch_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	slurm_step_id_t step_id = SLURM_STEP_ID_INITIALIZER;
	uint32_t priority = 0;
	job_record_t *job_ptr = NULL;
	job_desc_msg_t *job_desc_msg = msg->data;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition, read
	 * federation */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	char *err_msg = NULL, *job_submit_user_msg = NULL;
	bool reject_job = false;

	START_TIMER;
	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}

	if ((error_code = _valid_id("REQUEST_SUBMIT_BATCH_JOB", job_desc_msg,
				    msg->auth_uid, msg->auth_gid,
				    msg->protocol_version))) {
		reject_job = true;
		goto send_msg;
	}

	_set_hostname(msg, &job_desc_msg->alloc_node);
	_set_identity(msg, &job_desc_msg->id);

	if ((job_desc_msg->alloc_node == NULL) ||
	    (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_SUBMIT_BATCH_JOB lacks alloc_node from uid=%u",
		      msg->auth_uid);
	}

	dump_job_desc(job_desc_msg);

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			lock_slurmctld(job_read_lock);
		job_desc_msg->het_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,
						     msg->auth_uid, &err_msg);
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_read_lock);
	}

	/*
	 * In validate_job_create_req(), err_msg is currently only modified in
	 * the call to job_submit_g_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to fed_mgr_job_allocate and/or job_allocate, and we
	 * need the job submit plugin value to build the resource allocation
	 * response in the call to build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	if (error_code) {
		reject_job = true;
		goto send_msg;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}
	START_TIMER;	/* Restart after we have locks */

	if (fed_mgr_fed_rec) {
		if (fed_mgr_job_allocate(msg, job_desc_msg, false,
					 &step_id.job_id, &error_code,
					 &err_msg))
			reject_job = true;
		step_id.step_id = SLURM_BATCH_SCRIPT;
	} else {
		/* Create new job allocation */
		job_desc_msg->het_job_offset = NO_VAL;
		error_code = job_allocate(job_desc_msg,
					  job_desc_msg->immediate,
					  false, NULL, 0, msg->auth_uid, false,
					  &job_ptr, &err_msg,
					  msg->protocol_version);
		if (!job_ptr ||
		    (error_code && job_ptr->job_state == JOB_FAILED))
			reject_job = true;
		else {
			step_id = STEP_ID_FROM_JOB_RECORD(job_ptr);
			step_id.step_id = SLURM_BATCH_SCRIPT;
			priority = job_ptr->priority;
		}

		if (job_desc_msg->immediate &&
		    (error_code != SLURM_SUCCESS)) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			reject_job = true;
		}
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

send_msg:
	END_TIMER2(__func__);

	if (reject_job) {
		info("%s: %s", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		submit_response_msg_t submit_msg = {
			.step_id = step_id,
			.error_code = error_code,
			.job_submit_user_msg = job_submit_user_msg,
		};
		info("%s: %pI InitPrio=%u %s",
		     __func__, &step_id, priority, TIME_STR);
		/* send job_ID */
		(void) send_msg_response(msg, RESPONSE_SUBMIT_BATCH_JOB,
					 &submit_msg);

		schedule_job_save();	/* Has own locks */
		schedule_node_save();	/* Has own locks */
		queue_job_scheduler();
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_submit_batch_het_job - process RPC to submit a batch hetjob */
static void _slurm_rpc_submit_batch_het_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	list_itr_t *iter;
	int error_code = SLURM_SUCCESS, alloc_only = 0;
	DEF_TIMERS;
	slurm_step_id_t step_id = SLURM_STEP_ID_INITIALIZER;
	uint32_t het_job_offset = 0;
	job_record_t *job_ptr = NULL, *first_job_ptr = NULL;
	job_desc_msg_t *job_desc_msg;
	char *script = NULL;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node, read partition, read fed */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	list_t *job_req_list = msg->data;
	uint32_t job_uid = NO_VAL;
	char *err_msg = NULL, *job_submit_user_msg = NULL;
	bool reject_job = false;
	list_t *submit_job_list = NULL;
	hostset_t *jobid_hostset = NULL;
	char tmp_str[32];
	char *het_job_id_set = NULL;

	START_TIMER;
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_SUBMIT_BATCH_HET_JOB from uid=%u with empty job list",
		     msg->auth_uid);
		error_code = SLURM_ERROR;
		reject_job = true;
		goto send_msg;
	}
	if (!_sched_backfill()) {
		info("REQUEST_SUBMIT_BATCH_HET_JOB from uid=%u rejected as sched/backfill is not configured",
		     msg->auth_uid);
		error_code = ESLURM_NOT_SUPPORTED;
		reject_job = true;
		goto send_msg;
	}
	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_SUBMIT_BATCH_HET_JOB from uid=%u with empty job list",
		     msg->auth_uid);
		error_code = SLURM_ERROR;
		reject_job = true;
		goto send_msg;
	}

	/*
	 * If any job component has required nodes, those nodes must be excluded
	 * from all other components to avoid scheduling deadlock
	 */
	_exclude_het_job_nodes(job_req_list);

	/* Validate the individual request */
	lock_slurmctld(job_read_lock);     /* Locks for job_submit plugin use */
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = list_next(iter))) {
		if (job_uid == NO_VAL)
			job_uid = job_desc_msg->user_id;

		if ((error_code = _valid_id("REQUEST_SUBMIT_BATCH_JOB",
					    job_desc_msg, msg->auth_uid,
					    msg->auth_gid,
					    msg->protocol_version))) {
			reject_job = true;
			break;
		}

		_set_hostname(msg, &job_desc_msg->alloc_node);
		_set_identity(msg, &job_desc_msg->id);

		if ((job_desc_msg->alloc_node == NULL) ||
		    (job_desc_msg->alloc_node[0] == '\0')) {
			error("REQUEST_SUBMIT_BATCH_HET_JOB lacks alloc_node from uid=%u",
			      msg->auth_uid);
			error_code = ESLURM_INVALID_NODE_NAME;
			break;
		}

		dump_job_desc(job_desc_msg);

		job_desc_msg->het_job_offset = het_job_offset;
		error_code = validate_job_create_req(job_desc_msg,
						     msg->auth_uid,
						     &err_msg);
		if (err_msg) {
			char *save_ptr = NULL, *tok;
			tok = strtok_r(err_msg, "\n", &save_ptr);
			while (tok) {
				char *sep = "";
				if (job_submit_user_msg)
					sep = "\n";
				xstrfmtcat(job_submit_user_msg, "%s%d: %s",
					   sep, het_job_offset, tok);
				tok = strtok_r(NULL, "\n", &save_ptr);
			}
			xfree(err_msg);
		}

		if (error_code != SLURM_SUCCESS) {
			reject_job = true;
			break;
		}

		/* license request allowed only on leader */
		if (het_job_offset && job_desc_msg->licenses) {
			xstrfmtcat(job_submit_user_msg,
				   "%s%d: license request allowed only on leader job",
				   job_submit_user_msg ? "\n" : "",
				   het_job_offset);
			error("REQUEST_SUBMIT_BATCH_HET_JOB from uid=%u, license request on non-leader job",
			      msg->auth_uid);
			error_code = ESLURM_INVALID_LICENSES;
			reject_job = true;
			break;
		}

		het_job_offset++;
	}
	list_iterator_destroy(iter);
	unlock_slurmctld(job_read_lock);
	if (error_code != SLURM_SUCCESS)
		goto send_msg;

	/*
	 * In validate_job_create_req, err_msg is currently only modified in
	 * the call to job_submit_g_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to job_allocate, and we need the job submit plugin value
	 * to build the resource allocation response in the call to
	 * build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	/* Create new job allocations */
	submit_job_list = list_create(NULL);
	het_job_offset = 0;
	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	START_TIMER;	/* Restart after we have locks */
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = list_next(iter))) {
		if (!script)
			script = xstrdup(job_desc_msg->script);
		if (het_job_offset && job_desc_msg->script) {
			info("%s: Hetjob %u offset %u has script, being ignored",
			     __func__, step_id.job_id, het_job_offset);
			xfree(job_desc_msg->script);

		}
		if (het_job_offset) {
			/*
			 * Email notifications disabled except for
			 * hetjob leader
			 */
			job_desc_msg->mail_type = 0;
			xfree(job_desc_msg->mail_user);
		}
		if (!job_desc_msg->burst_buffer) {
			xfree(job_desc_msg->script);
			if (!(job_desc_msg->script = bb_g_build_het_job_script(
				      script, het_job_offset))) {
				error_code =
					ESLURM_INVALID_BURST_BUFFER_REQUEST;
				reject_job = true;
				break;
			}
		}
		job_desc_msg->het_job_offset = het_job_offset;
		error_code = job_allocate(job_desc_msg,
					  job_desc_msg->immediate, false,
					  NULL, alloc_only, msg->auth_uid,
					  false, &job_ptr, &err_msg,
					  msg->protocol_version);
		if (!job_ptr ||
		    (error_code && job_ptr->job_state == JOB_FAILED)) {
			reject_job = true;
		} else {
			if (step_id.job_id == NO_VAL) {
				step_id = STEP_ID_FROM_JOB_RECORD(job_ptr);
				step_id.step_id = SLURM_BATCH_SCRIPT;
				first_job_ptr = job_ptr;
				alloc_only = 1;
			}
			snprintf(tmp_str, sizeof(tmp_str), "%u",
				 job_ptr->job_id);
			if (jobid_hostset)
				hostset_insert(jobid_hostset, tmp_str);
			else
				jobid_hostset = hostset_create(tmp_str);
			job_ptr->het_job_id = step_id.job_id;
			job_ptr->het_job_offset = het_job_offset++;
			job_ptr->batch_flag      = 1;
			on_job_state_change(job_ptr, job_ptr->job_state);
			list_append(submit_job_list, job_ptr);
		}

		if (job_desc_msg->immediate &&
		    (error_code != SLURM_SUCCESS)) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			reject_job = true;
		}
		if (reject_job)
			break;
	}
	list_iterator_destroy(iter);
	xfree(script);

	if ((step_id.job_id == NO_VAL) && !reject_job) {
		info("%s: No error, but no het_job_id", __func__);
		error_code = SLURM_ERROR;
		reject_job = true;
	}

	/* Validate limits on hetjob as a whole */
	if (!reject_job &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    !acct_policy_validate_het_job(submit_job_list)) {
		info("Hetjob %pI exceeded association/QOS limit for user %u",
		     &step_id, job_uid);
		error_code = ESLURM_ACCOUNTING_POLICY;
		reject_job = true;
	}

	_create_het_job_id_set(jobid_hostset, het_job_offset,
			       &het_job_id_set);
	if (first_job_ptr)
		first_job_ptr->het_job_list = submit_job_list;

	iter = list_iterator_create(submit_job_list);
	while ((job_ptr = list_next(iter))) {
		job_ptr->het_job_id_set = xstrdup(het_job_id_set);
		if (error_code == SLURM_SUCCESS)
			log_flag(HETJOB, "Submit %pJ", job_ptr);
	}
	list_iterator_destroy(iter);
	xfree(het_job_id_set);

	if (reject_job && submit_job_list) {
		(void) list_for_each(submit_job_list, _het_job_cancel, NULL);
		if (!first_job_ptr)
			FREE_NULL_LIST(submit_job_list);
	}

	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);

send_msg:
	END_TIMER2(__func__);
	if (reject_job) {
		info("%s: %s", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		submit_response_msg_t submit_msg = {
			.step_id = step_id,
			.error_code = error_code,
			.job_submit_user_msg = job_submit_user_msg,
		};
		info("%s: %pI %s", __func__, &step_id, TIME_STR);
		/* send job_ID */
		(void) send_msg_response(msg, RESPONSE_SUBMIT_BATCH_JOB,
					 &submit_msg);

		schedule_job_save();	/* Has own locks */
	}
	if (jobid_hostset)
		hostset_destroy(jobid_hostset);
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_update_job - process RPC to update the configuration of a
 * job (e.g. priority)
 */
static void _slurm_rpc_update_job(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg = msg->data;
	/* Locks: Read config, write job, write node, read partition, read fed*/
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = msg->auth_uid;

	lock_slurmctld(fed_read_lock);
	if (!_route_msg_to_origin(msg, job_desc_msg->job_id_str,
				  job_desc_msg->step_id.job_id)) {
		unlock_slurmctld(fed_read_lock);
		return;
	}
	unlock_slurmctld(fed_read_lock);

	START_TIMER;

	/* job_desc_msg->user_id is set when the uid has been overridden with
	 * -u <uid> or --uid=<uid>. NO_VAL is default. Verify the request has
	 * come from an admin */
	if (job_desc_msg->user_id != SLURM_AUTH_NOBODY) {
		if (!validate_super_user(uid)) {
			error_code = ESLURM_USER_ID_MISSING;
			error("Security violation, REQUEST_UPDATE_JOB RPC from uid=%u",
			      uid);
			/* Send back the error message for this case because
			 * update_job also sends back an error message */
			slurm_send_rc_msg(msg, error_code);
		} else {
			/* override uid allowed */
			uid = job_desc_msg->user_id;
		}
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		dump_job_desc(job_desc_msg);
		/* Ensure everything that may be written to database is lower
		 * case */
		xstrtolower(job_desc_msg->account);
		xstrtolower(job_desc_msg->wckey);

		/*
		 * Use UID provided by scontrol. May be overridden with
		 * -u <uid>  or --uid=<uid>
		 */
		lock_slurmctld(job_write_lock);
		if (job_desc_msg->job_id_str)
			error_code = update_job_str(msg, uid);
		else
			error_code = update_job(msg, uid, true);
		unlock_slurmctld(job_write_lock);
	}
	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		if (job_desc_msg->job_id_str) {
			info("%s: JobId=%s uid=%u: %s",
			     __func__, job_desc_msg->job_id_str, uid,
			     slurm_strerror(error_code));
		} else {
			info("%s: %pI uid=%u: %s",
			     __func__, &job_desc_msg->step_id, uid,
			     slurm_strerror(error_code));
		}
	} else {
		if (job_desc_msg->job_id_str) {
			info("%s: complete JobId=%s uid=%u %s",
			     __func__, job_desc_msg->job_id_str, uid, TIME_STR);
		} else {
			info("%s: complete %pI uid=%u %s",
			     __func__, &job_desc_msg->step_id, uid, TIME_STR);
		}
		/* Below functions provide their own locking */
		schedule_job_save();
		schedule_node_save();
		queue_job_scheduler();
	}
}

/*
 * _slurm_rpc_create_node - process RPC to create node(s).
 */
static void _slurm_rpc_create_node(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_node_msg_t *node_msg = msg->data;
	char *err_msg = NULL;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, %s RPC from uid=%u",
		      rpc_num2string(msg->msg_type), msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		error_code = create_nodes(node_msg, &err_msg);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s for %s: %s",
		     __func__, node_msg->node_names,
		     slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, node_msg->node_names, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
	xfree(err_msg);

	/* Below functions provide their own locks */
	schedule_node_save();
	validate_all_reservations(false, false);
	queue_job_scheduler();
	trigger_reconfig();
}

/*
 * _slurm_rpc_update_node - process RPC to update the configuration of a
 *	node (e.g. UP/DOWN)
 */
static void _slurm_rpc_update_node(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_node_msg_t *update_node_msg_ptr = msg->data;
	/* Locks: Write job, partition and node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_NODE RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = update_node(update_node_msg_ptr, msg->auth_uid);
		unlock_slurmctld(node_write_lock);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s for %s: %s",
		     __func__, update_node_msg_ptr->node_names,
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, update_node_msg_ptr->node_names, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

	/* Below functions provide their own locks */
	schedule_node_save();
	validate_all_reservations(false, false);
	queue_job_scheduler();
	trigger_reconfig();
}

/*
 * _slurm_rpc_delete_node - process RPC to delete node.
 */
static void _slurm_rpc_delete_node(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	update_node_msg_t *node_msg = msg->data;
	char *err_msg = NULL;
	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_NODE RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		error_code = delete_nodes(node_msg->node_names, &err_msg);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s for %s: %s",
		     __func__, node_msg->node_names,
		     slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, node_msg->node_names, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
	xfree(err_msg);

	/* Below functions provide their own locks */
	schedule_node_save();
	validate_all_reservations(false, false);
	queue_job_scheduler();
	trigger_reconfig();
}

/* _slurm_rpc_update_partition - process RPC to update the configuration
 *	of a partition (e.g. UP/DOWN) */
static void _slurm_rpc_update_partition(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_part_msg_t *part_desc_ptr = msg->data;
	/* Locks: Read config, write job, write node, write partition
	 * NOTE: job write lock due to gang scheduler support */
	slurmctld_lock_t part_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_PARTITION RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		if (msg->msg_type == REQUEST_CREATE_PARTITION) {
			lock_slurmctld(part_write_lock);
			error_code = update_part(part_desc_ptr, true);
			unlock_slurmctld(part_write_lock);
		} else {
			lock_slurmctld(part_write_lock);
			error_code = update_part(part_desc_ptr, false);
			unlock_slurmctld(part_write_lock);
		}
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s partition=%s: %s",
		     __func__, part_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, part_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		schedule_part_save();		/* Has its locking */
		queue_job_scheduler();
	}
}

/* _slurm_rpc_delete_partition - process RPC to delete a partition */
static void _slurm_rpc_delete_partition(slurm_msg_t *msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	delete_part_msg_t *part_desc_ptr = msg->data;
	/* Locks: write job, write node, write partition */
	slurmctld_lock_t part_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_PARTITION RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(part_write_lock);
		error_code = delete_partition(part_desc_ptr);
		unlock_slurmctld(part_write_lock);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s partition=%s: %s",
		     __func__, part_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("%s complete for %s %s",
		     __func__, part_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		save_all_state();	/* Has own locking */
		queue_job_scheduler();
	}
}

/* _slurm_rpc_resv_create - process RPC to create a reservation */
static void _slurm_rpc_resv_create(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	char *err_msg = NULL;
	DEF_TIMERS;
	resv_desc_msg_t *resv_desc_ptr = msg->data;
	/* Locks: read config, read job, write node, read partition */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	if (!validate_operator(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, CREATE_RESERVATION RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = create_resv(resv_desc_ptr, &err_msg);
		unlock_slurmctld(node_write_lock);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		if (resv_desc_ptr->name) {
			info("%s reservation=%s: %s",
			     __func__, resv_desc_ptr->name,
			     slurm_strerror(error_code));
		} else {
			info("%s: %s", __func__, slurm_strerror(error_code));
		}
		slurm_send_rc_err_msg(msg, error_code, err_msg);
	} else {
		reservation_name_msg_t resv_resp_msg = {
			.name = resv_desc_ptr->name,
		};

		debug2("%s complete for %s %s",
		       __func__, resv_desc_ptr->name, TIME_STR);
		/* send reservation name */
		(void) send_msg_response(msg, RESPONSE_CREATE_RESERVATION,
					 &resv_resp_msg);

		queue_job_scheduler();
	}
	xfree(err_msg);
}

/* _slurm_rpc_resv_update - process RPC to update a reservation */
static void _slurm_rpc_resv_update(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	char *err_msg = NULL;
	DEF_TIMERS;
	resv_desc_msg_t *resv_desc_ptr = msg->data;
	/* Locks: read config, read job, write node, read partition */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(node_write_lock);
	if (!validate_operator(msg->auth_uid)) {
		if (!validate_resv_uid(resv_desc_ptr->name, msg->auth_uid) ||
		    !(resv_desc_ptr->flags & RESERVE_FLAG_SKIP)) {
			error_code = ESLURM_USER_ID_MISSING;
			error("Security violation, UPDATE_RESERVATION RPC from uid=%u",
			      msg->auth_uid);
		} else {
			resv_desc_msg_t *resv_desc_ptr2 =
				xmalloc_nz(sizeof(*resv_desc_ptr2));
			slurm_init_resv_desc_msg(resv_desc_ptr2);
			/*
			 * Sanitize the structure since a regular user is doing
			 * this and is only allowed to skip the reservation and
			 * not update anything else.
			 */
			resv_desc_ptr2->name = resv_desc_ptr->name;
			resv_desc_ptr->name = NULL;
			resv_desc_ptr2->flags = RESERVE_FLAG_SKIP;
			slurm_free_resv_desc_msg(resv_desc_ptr);
			resv_desc_ptr = msg->data = resv_desc_ptr2;
		}
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		error_code = update_resv(resv_desc_ptr, &err_msg);
		END_TIMER2(__func__);
	}
	unlock_slurmctld(node_write_lock);

	/* return result */
	if (error_code) {
		info("%s reservation=%s: %s",
		     __func__, resv_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_err_msg(msg, error_code, err_msg);
	} else {
		debug2("%s complete for %s %s",
		       __func__, resv_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		queue_job_scheduler();
	}
	xfree(err_msg);
}

/* _slurm_rpc_resv_delete - process RPC to delete a reservation */
static void _slurm_rpc_resv_delete(slurm_msg_t *msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	reservation_name_msg_t *resv_desc_ptr = msg->data;
	/* Locks: write job, write node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	/* node_write_lock needed for validate_resv_uid */
	lock_slurmctld(node_write_lock);
	if (!validate_operator(msg->auth_uid) &&
	    !validate_resv_uid(resv_desc_ptr->name, msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_RESERVATION RPC from uid=%u",
		      msg->auth_uid);
	} else if (!resv_desc_ptr->name) {
		error_code = ESLURM_INVALID_PARTITION_NAME;
		error("Invalid DELETE_RESERVATION RPC from uid=%u, name is null",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		error_code = delete_resv(resv_desc_ptr);
		END_TIMER2(__func__);
	}
	unlock_slurmctld(node_write_lock);

	/* return result */
	if (error_code) {
		info("%s reservation=%s: %s",
		     __func__, resv_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("%s complete for %s %s",
		     __func__, resv_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		queue_job_scheduler();
	}
}

/* _slurm_rpc_resv_show - process RPC to dump reservation info */
static void _slurm_rpc_resv_show(slurm_msg_t *msg)
{
	resv_info_request_msg_t *resv_req_msg = msg->data;
	buf_t *buffer = NULL;
	DEF_TIMERS;
	/* Locks: read node */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	if ((resv_req_msg->last_update - 1) >= last_resv_update) {
		debug2("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		lock_slurmctld(node_read_lock);
		buffer = show_resv(msg->auth_uid, msg->protocol_version);
		unlock_slurmctld(node_read_lock);
		END_TIMER2(__func__);

		/* send message */
		(void) send_msg_response(msg, RESPONSE_RESERVATION_INFO, buffer);
		FREE_NULL_BUFFER(buffer);
	}
}

static void _slurm_rpc_node_registration_status(slurm_msg_t *msg)
{
	error("slurmctld is talking with itself. SlurmctldPort == SlurmdPort");
	slurm_send_rc_msg(msg, EINVAL);
}

/* determine of nodes are ready for the job */
static void _slurm_rpc_job_ready(slurm_msg_t *msg)
{
	int error_code, result;
	job_id_msg_t *id_msg = msg->data;
	DEF_TIMERS;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);
	error_code = job_node_ready(&id_msg->step_id, &result);
	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (error_code) {
		debug2("%s: %s", __func__, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		return_code_msg_t rc_msg = {
			.return_code = result,
		};

		debug2("%s: %pI result %d in %s",
		       __func__, &id_msg->step_id, result, TIME_STR);

		if (!_is_prolog_finished(&id_msg->step_id))
			(void) send_msg_response(msg, RESPONSE_PROLOG_EXECUTING,
						 &rc_msg);
		else
			(void) send_msg_response(msg, RESPONSE_JOB_READY,
						 &rc_msg);
	}
}

/* Check if prolog has already finished */
static int _is_prolog_finished(slurm_step_id_t *step_id)
{
	int is_running = 0;
	job_record_t *job_ptr;

	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	lock_slurmctld(job_read_lock);
	if ((job_ptr = find_job(step_id)))
		is_running = (job_ptr->state_reason != WAIT_PROLOG);
	unlock_slurmctld(job_read_lock);
	return is_running;
}

/* get node select info plugin */
static void _slurm_rpc_burst_buffer_info(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	buf_t *buffer;
	uid_t uid = msg->auth_uid;
	DEF_TIMERS;

	START_TIMER;
	buffer = init_buf(BUF_SIZE);
	if (validate_super_user(msg->auth_uid))
		uid = 0;
	error_code = bb_g_state_pack(uid, buffer, msg->protocol_version);
	END_TIMER2(__func__);

	if (error_code) {
		debug("%s: %s", __func__, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		(void) send_msg_response(msg, RESPONSE_BURST_BUFFER_INFO,
					 buffer);
		FREE_NULL_BUFFER(buffer);
	}
}

static void _slurm_rpc_suspend(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	suspend_msg_t *sus_ptr = msg->data;
	/* Locks: write job and node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	job_record_t *job_ptr;
	char *op;

	START_TIMER;
	switch (sus_ptr->op) {
	case SUSPEND_JOB:
		op = "suspend";
		break;
	case RESUME_JOB:
		op = "resume";
		break;
	default:
		op = "unknown";
	}
	debug3("Processing RPC details: REQUEST_SUSPEND(%s)", op);

	/* Get the job id part of the jobid. It could be an array id. Currently
	 * in a federation, job arrays only run on the origin cluster so we just
	 * want to find if the array, not a specific task, is on the origin
	 * cluster. */
	if ((sus_ptr->step_id.job_id == NO_VAL) && sus_ptr->job_id_str)
		sus_ptr->step_id.job_id = strtol(sus_ptr->job_id_str, NULL, 10);

	lock_slurmctld(job_write_lock);
	job_ptr = find_job(&sus_ptr->step_id);

	/* If job is found on the cluster, it could be pending, the origin
	 * cluster, or running on the sibling cluster. If it's not there then
	 * route it to the origin, otherwise try to suspend the job. If it's
	 * pending an error should be returned. If it's running then it should
	 * suspend the job. */
	if (!job_ptr &&
	    !_route_msg_to_origin(msg, NULL, sus_ptr->step_id.job_id)) {
		unlock_slurmctld(job_write_lock);
		return;
	}
	if (!job_ptr)
		error_code = ESLURM_INVALID_JOB_ID;
	else if (fed_mgr_job_started_on_sib(job_ptr)) {

		/* Route to the cluster that is running the job. */
		slurmdb_cluster_rec_t *dst =
			fed_mgr_get_cluster_by_id(
					job_ptr->fed_details->cluster_lock);
		if (dst) {
			slurm_send_reroute_msg(msg, dst, NULL);
			info("%s: %s %pJ uid %u routed to %s",
			     __func__, rpc_num2string(msg->msg_type),
			     job_ptr, msg->auth_uid, dst->name);

			unlock_slurmctld(job_write_lock);
			END_TIMER2(__func__);
			return;
		}

		error("couldn't find cluster by cluster id %d",
		      job_ptr->fed_details->cluster_lock);
		error_code = ESLURM_INVALID_CLUSTER_NAME;
	} else if (sus_ptr->job_id_str) {
		error_code = job_suspend2(msg, sus_ptr, msg->auth_uid, true,
					  msg->protocol_version);
	} else {
		error_code = job_suspend(msg, sus_ptr, msg->auth_uid,
					 true, msg->protocol_version);
	}
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	if (!sus_ptr->job_id_str)
		xstrfmtcat(sus_ptr->job_id_str, "%u", sus_ptr->step_id.job_id);

	if (error_code) {
		info("%s(%s) for %s %s",
		     __func__, op, sus_ptr->job_id_str,
		     slurm_strerror(error_code));
	} else {
		info("%s(%s) for %s %s",
		     __func__, op, sus_ptr->job_id_str, TIME_STR);

		schedule_job_save();	/* Has own locking */
		if (sus_ptr->op == SUSPEND_JOB)
			queue_job_scheduler();
	}
}

static void _slurm_rpc_top_job(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	top_job_msg_t *top_ptr = msg->data;
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_write_lock);
	error_code = job_set_top(msg, top_ptr, msg->auth_uid,
				 msg->protocol_version);
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	if (error_code) {
		info("%s for %s %s",
		     __func__, top_ptr->job_id_str, slurm_strerror(error_code));
	} else {
		info("%s for %s %s",
		     __func__, top_ptr->job_id_str, TIME_STR);
	}
}

static void _slurm_rpc_auth_token(slurm_msg_t *msg)
{
	DEF_TIMERS;
	token_request_msg_t *request_msg = msg->data;
	token_response_msg_t *resp_data;
	char *auth_username = NULL, *username = NULL;
	int lifespan = 0;
	static int max_lifespan = -1;

	START_TIMER;
	if (xstrstr(slurm_conf.authalt_params, "disable_token_creation") &&
	    !validate_slurm_user(msg->auth_uid)) {
		error("%s: attempt to retrieve a token while token creation disabled UID=%u",
		      __func__, msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	if (!auth_is_plugin_type_inited(AUTH_PLUGIN_JWT)) {
		slurm_send_rc_msg(msg, ESLURM_PLUGIN_NOT_LOADED);
		return;
	}

	if (max_lifespan == -1) {
		char *tmp_ptr;

		max_lifespan = 0;
		if ((tmp_ptr = xstrcasestr(slurm_conf.authalt_params,
					   "max_token_lifespan="))) {
			max_lifespan = atoi(tmp_ptr + 19);
			if (max_lifespan < 1) {
				error("Invalid AuthAltParameters max_token_lifespan option, no limit enforced");
				max_lifespan = 0;
			}
		}
	}

	auth_username = uid_to_string_or_null(msg->auth_uid);

	if (request_msg->username) {
		if (validate_slurm_user(msg->auth_uid)) {
			username = request_msg->username;
		} else if (!xstrcmp(request_msg->username, auth_username)) {
			/* user explicitly provided their own username */
			username = request_msg->username;
		} else {
			error("%s: attempt to retrieve a token for a different user=%s by UID=%u",
			      __func__, request_msg->username, msg->auth_uid);
			xfree(auth_username);
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
			return;
		}
	} else if (!auth_username) {
		error("%s: attempt to retrieve a token for a missing username by UID=%u",
		      __func__, msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	} else {
		username = auth_username;
	}

	if (request_msg->lifespan)
		lifespan = request_msg->lifespan;
	else if (max_lifespan)
		lifespan = MIN(DEFAULT_AUTH_TOKEN_LIFESPAN, max_lifespan);
	else
		lifespan = DEFAULT_AUTH_TOKEN_LIFESPAN;

	if (!validate_slurm_user(msg->auth_uid)) {
		if ((max_lifespan > 0) && (lifespan > max_lifespan)) {
			error("%s: rejecting token lifespan %d for user:%s[%d] requested, exceeds limit of %d",
			      __func__, request_msg->lifespan, username,
			      msg->auth_uid, max_lifespan);
			xfree(auth_username);
			slurm_send_rc_msg(msg, ESLURM_INVALID_TIME_LIMIT);
			return;
		}
	}

	resp_data = xmalloc(sizeof(*resp_data));
	resp_data->token = auth_g_token_generate(AUTH_PLUGIN_JWT, username,
						 lifespan);
	xfree(auth_username);
	END_TIMER2(__func__);

	if (!resp_data->token) {
		error("%s: error generating auth token: %m", __func__);
		xfree(resp_data);
		slurm_send_rc_msg(msg, ESLURM_AUTH_UNABLE_TO_GENERATE_TOKEN);
		return;
	}

	(void) send_msg_response(msg, RESPONSE_AUTH_TOKEN, resp_data);
	slurm_free_token_response_msg(resp_data);
}

static void _slurm_rpc_requeue(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	requeue_msg_t *req_ptr = msg->data;
	/* Locks: write job and node */
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };

	lock_slurmctld(fed_read_lock);
	if (!_route_msg_to_origin(msg, req_ptr->job_id_str,
				  req_ptr->step_id.job_id)) {
		unlock_slurmctld(fed_read_lock);
		return;
	}
	unlock_slurmctld(fed_read_lock);

	START_TIMER;
	lock_slurmctld(job_write_lock);
	if (req_ptr->job_id_str) {
		error_code = job_requeue2(msg->auth_uid, req_ptr, msg, false);
	} else {
		error_code =
			job_requeue_external(msg->auth_uid, &req_ptr->step_id,
					     false, req_ptr->flags);
	}
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	if (!req_ptr->job_id_str)
		slurm_send_rc_msg(msg, error_code);

	if (error_code) {
		if (!req_ptr->job_id_str)
			xstrfmtcat(req_ptr->job_id_str, "%u",
				   req_ptr->step_id.job_id);

		info("%s: Requeue of JobId=%s returned an error: %s",
		     __func__, req_ptr->job_id_str, slurm_strerror(error_code));
	}

	/* Functions below provide their own locking
	 */
	schedule_job_save();
}

/* Copy an array of type char **, xmalloc() the array and xstrdup() the
 * strings in the array */
extern char **
xduparray(uint32_t size, char ** array)
{
	int i;
	char ** result;

	if (size == 0)
		return (char **)NULL;

	result = xcalloc(size, sizeof(char *));
	for (i=0; i<size; i++)
		result[i] = xstrdup(array[i]);

	return result;
}

static void _slurm_rpc_trigger_clear(slurm_msg_t *msg)
{
	int rc;
	trigger_info_msg_t *trigger_ptr = msg->data;
	DEF_TIMERS;

	START_TIMER;
	rc = trigger_clear(msg->auth_uid, trigger_ptr);
	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_trigger_get(slurm_msg_t *msg)
{
	trigger_info_msg_t *resp_data;
	trigger_info_msg_t *trigger_ptr = msg->data;
	DEF_TIMERS;

	START_TIMER;
	resp_data = trigger_get(msg->auth_uid, trigger_ptr);
	END_TIMER2(__func__);

	(void) send_msg_response(msg, RESPONSE_TRIGGER_GET, resp_data);
	slurm_free_trigger_msg(resp_data);
}

static void _slurm_rpc_trigger_set(slurm_msg_t *msg)
{
	int rc;
	trigger_info_msg_t *trigger_ptr = msg->data;
	bool allow_user_triggers = xstrcasestr(slurm_conf.slurmctld_params,
	                                       "allow_user_triggers");
	bool disable_triggers = xstrcasestr(slurm_conf.slurmctld_params,
					    "disable_triggers");
	DEF_TIMERS;

	START_TIMER;
	if (disable_triggers) {
		rc = ESLURM_DISABLED;
		error("Request to set trigger, but disable_triggers is set.");
	} else if (validate_slurm_user(msg->auth_uid) || allow_user_triggers) {
		rc = trigger_set(msg->auth_uid, msg->auth_gid, trigger_ptr);
	} else {
		rc = ESLURM_ACCESS_DENIED;
		error("Security violation, REQUEST_TRIGGER_SET RPC from uid=%u",
		      msg->auth_uid);
	}
	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_trigger_pull(slurm_msg_t *msg)
{
	int rc;
	trigger_info_msg_t *trigger_ptr = msg->data;
	DEF_TIMERS;

	START_TIMER;
	/* NOTE: No locking required here, trigger_pull only needs to lock
	 * it's own internal trigger structure */
	if (!validate_slurm_user(msg->auth_uid)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_TRIGGER_PULL RPC from uid=%u",
		      msg->auth_uid);
	} else
		rc = trigger_pull(trigger_ptr);
	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_get_topo(slurm_msg_t *msg)
{
	int rc;
	topo_info_response_msg_t *topo_resp_msg;
	topo_info_request_msg_t *topo_req_msg = msg->data;
	/* Locks: read node lock */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	DEF_TIMERS;

	topo_resp_msg = xmalloc(sizeof(topo_info_response_msg_t));
	START_TIMER;
	lock_slurmctld(node_read_lock);
	rc = topology_g_get(TOPO_DATA_TOPOLOGY_PTR, topo_req_msg->name,
			    &topo_resp_msg->topo_info);
	unlock_slurmctld(node_read_lock);
	END_TIMER2(__func__);

	if (rc) {
		slurm_send_rc_msg(msg, rc);
	} else {
		(void) send_msg_response(msg, RESPONSE_TOPO_INFO,
					 topo_resp_msg);
	}
	slurm_free_topo_info_msg(topo_resp_msg);
}

static void _slurm_rpc_get_topo_config(slurm_msg_t *msg)
{
	topo_config_response_msg_t *topo_resp_msg;
	slurmctld_lock_t node_read_lock = {
		.node = READ_LOCK,
	};
	DEF_TIMERS;

	topo_resp_msg = xmalloc(sizeof(*topo_resp_msg));
	START_TIMER;
	lock_slurmctld(node_read_lock);
	topo_resp_msg->config = topology_g_get_config();
	unlock_slurmctld(node_read_lock);
	END_TIMER2(__func__);

	(void) send_msg_response(msg, RESPONSE_TOPO_CONFIG, topo_resp_msg);
	slurm_free_topo_config_msg(topo_resp_msg);
}

static void _slurm_rpc_job_notify(slurm_msg_t *msg)
{
	int error_code;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	job_notify_msg_t *notify_msg = msg->data;
	job_record_t *job_ptr;
	DEF_TIMERS;

	START_TIMER;
	lock_slurmctld(job_read_lock);
	job_ptr = find_job(&notify_msg->step_id);

	/* If job is found on the cluster, it could be pending, the origin
	 * cluster, or running on the sibling cluster. If it's not there then
	 * route it to the origin. */
	if (!job_ptr &&
	    !_route_msg_to_origin(msg, NULL, notify_msg->step_id.job_id)) {
		unlock_slurmctld(job_read_lock);
		return;
	}

	if (!job_ptr)
		error_code = ESLURM_INVALID_JOB_ID;
	else if (job_ptr->batch_flag &&
		 fed_mgr_job_started_on_sib(job_ptr)) {

		/* Route to the cluster that is running the batch job. srun jobs
		 * don't need to be routed to the running cluster since the
		 * origin cluster knows how to contact the listening srun. */
		slurmdb_cluster_rec_t *dst =
			fed_mgr_get_cluster_by_id(
					job_ptr->fed_details->cluster_lock);
		if (dst) {
			slurm_send_reroute_msg(msg, dst, NULL);
			info("%s: %s %pJ uid %u routed to %s",
			     __func__, rpc_num2string(msg->msg_type),
			     job_ptr, msg->auth_uid, dst->name);

			unlock_slurmctld(job_read_lock);
			END_TIMER2(__func__);
			return;
		}

		error("couldn't find cluster by cluster id %d",
		      job_ptr->fed_details->cluster_lock);
		error_code = ESLURM_INVALID_CLUSTER_NAME;

	} else if ((job_ptr->user_id == msg->auth_uid) ||
		   validate_slurm_user(msg->auth_uid))
		error_code = srun_user_message(job_ptr, notify_msg->message);
	else {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_JOB_NOTIFY RPC from uid=%u for %pJ owner %u",
		      msg->auth_uid, job_ptr, job_ptr->user_id);
	}
	unlock_slurmctld(job_read_lock);

	END_TIMER2(__func__);
	slurm_send_rc_msg(msg, error_code);
}

static void _slurm_rpc_set_debug_flags(slurm_msg_t *msg)
{
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	set_debug_flags_msg_t *request_msg = msg->data;
	char *flag_string;

	if (!validate_super_user(msg->auth_uid)) {
		error("set debug flags request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	lock_slurmctld (config_write_lock);
	slurm_conf.debug_flags &= (~request_msg->debug_flags_minus);
	slurm_conf.debug_flags |= request_msg->debug_flags_plus;
	slurm_conf.last_update = time(NULL);
	slurmscriptd_update_debug_flags(slurm_conf.debug_flags);

	/* Reset cached debug_flags values */
	gs_reconfig();
	gres_reconfig();
	priority_g_reconfig(false);
	select_g_reconfigure();
	(void) sched_g_reconfig();

	unlock_slurmctld (config_write_lock);
	flag_string = debug_flags2str(slurm_conf.debug_flags);
	info("Set DebugFlags to %s", flag_string ? flag_string : "none");
	xfree(flag_string);
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_set_debug_level(slurm_msg_t *msg)
{
	int debug_level;
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	set_debug_level_msg_t *request_msg = msg->data;

	if (!validate_super_user(msg->auth_uid)) {
		error("set debug level request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	/* NOTE: not offset by LOG_LEVEL_INFO, since it's inconvenient
	 * to provide negative values for scontrol */
	debug_level = MIN (request_msg->debug_level, (LOG_LEVEL_END - 1));
	debug_level = MAX (debug_level, LOG_LEVEL_QUIET);

	lock_slurmctld(config_write_lock);
	update_log_levels(debug_level, debug_level);
	slurmscriptd_update_log_level(debug_level, false);

	info("Set debug level to '%s'", log_num2string(debug_level));

	slurm_conf.slurmctld_debug = debug_level;
	slurm_conf.last_update = time(NULL);
	unlock_slurmctld(config_write_lock);

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static char *_update_hostset_from_mode(char *update_str,
				       update_mode_t mode,
				       char *current_str)
{
	char *new_str = NULL;

	if (mode == UPDATE_SET) {
		if (*update_str)
			new_str = xstrdup(update_str);
	} else {
		hostset_t *current_hostset = hostset_create(current_str);
		if (mode == UPDATE_ADD) {
			hostset_insert(current_hostset, update_str);
		} else if (mode == UPDATE_REMOVE) {
			hostset_delete(current_hostset, update_str);
		} /* If bad mode is sent do nothing */

		if (hostset_count(current_hostset))
			new_str =
				hostset_ranged_string_xmalloc(current_hostset);
		hostset_destroy(current_hostset);
	}
	return new_str;
}

static char *_update_string_from_mode(char *update_str,
				      update_mode_t mode,
				      char *current_str,
				      bool lower_case_normalization)
{
	char *new_str = NULL;

	if (mode == UPDATE_ADD) {
		if (current_str && *current_str) {
			list_t *current_list = list_create(xfree_ptr);

			slurm_addto_char_list_with_case(
				current_list, current_str,
				lower_case_normalization);
			if (*update_str)
				slurm_addto_char_list_with_case(
					current_list, update_str,
					lower_case_normalization);
			new_str = slurm_char_list_to_xstr(current_list);

			FREE_NULL_LIST(current_list);
		} else if (*update_str) {
			new_str = xstrdup(update_str);
		}
	} else if (mode == UPDATE_REMOVE) {
		if (current_str && *current_str) {
			list_t *current_list = list_create(xfree_ptr);
			list_t *rem_list = list_create(xfree_ptr);

			slurm_addto_char_list_with_case(
				current_list, current_str,
				lower_case_normalization);
			slurm_addto_char_list_with_case(
				rem_list, update_str,
				lower_case_normalization);

			slurm_remove_char_list_from_char_list(current_list,
							      rem_list);
			new_str = slurm_char_list_to_xstr(current_list);

			FREE_NULL_LIST(current_list);
			FREE_NULL_LIST(rem_list);
		}
	} else if (mode == UPDATE_SET) {
		if (*update_str)
			new_str = xstrdup(update_str);
	} else	{ /* If bad mode is sent do nothing */
		error("bad update mode %d", mode);
		if (current_str && *current_str)
			new_str = xstrdup(current_str);
	}

	return new_str;
}

static void _set_power_save_settings(char *new_str, char **slurm_conf_setting)
{
	slurmctld_lock_t locks = {
		.conf = WRITE_LOCK,
		.node = READ_LOCK,
		.part = READ_LOCK,
	};

	lock_slurmctld(locks);
	xfree(*slurm_conf_setting);
	*slurm_conf_setting = new_str;
	slurm_conf.last_update = time(NULL);
	power_save_exc_setup(); /* Reload power save settings */
	unlock_slurmctld(locks);
}

static void _slurm_rpc_set_suspend_exc_nodes(slurm_msg_t *msg)
{
	suspend_exc_update_msg_t *update_msg = msg->data;
	char *new_str;

	if (!validate_super_user(msg->auth_uid)) {
		error("set SuspendExcNodes request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	if ((update_msg->mode != UPDATE_SET) &&
	    (xstrchr(slurm_conf.suspend_exc_nodes, ':') ||
	     xstrchr(update_msg->update_str, ':'))) {
		error("Append and remove from SuspendExcNodes with ':' is not supported. Please use direct assignment instead.");
		slurm_send_rc_msg(msg, ESLURM_INVALID_NODE_NAME);
		return;
	}

	new_str = _update_hostset_from_mode(update_msg->update_str,
					    update_msg->mode,
					    slurm_conf.suspend_exc_nodes);

	if (!xstrcmp(new_str, slurm_conf.suspend_exc_nodes)) {
		info("SuspendExcNodes did not change from %s with update: %s",
		      slurm_conf.suspend_exc_nodes, update_msg->update_str);
		xfree(new_str);
	} else {
		info("Setting SuspendExcNodes to '%s'", new_str);
		_set_power_save_settings(new_str,
					 &slurm_conf.suspend_exc_nodes);
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_set_suspend_exc_parts(slurm_msg_t *msg)
{
	suspend_exc_update_msg_t *update_msg = msg->data;
	char *new_str;

	if (!validate_super_user(msg->auth_uid)) {
		error("set SuspendExcParts request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	new_str = _update_string_from_mode(update_msg->update_str,
					   update_msg->mode,
					   slurm_conf.suspend_exc_parts, false);

	if (!xstrcmp(new_str, slurm_conf.suspend_exc_parts)) {
		info("SuspendExcParts did not change from %s with update: %s",
		      slurm_conf.suspend_exc_parts, update_msg->update_str);
		xfree(new_str);
	} else {
		info("Setting SuspendExcParts to '%s'", new_str);
		_set_power_save_settings(new_str,
					 &slurm_conf.suspend_exc_parts);
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_set_suspend_exc_states(slurm_msg_t *msg)
{
	suspend_exc_update_msg_t *update_msg = msg->data;
	char *new_str;

	if (!validate_super_user(msg->auth_uid)) {
		error("set SuspendExcStates request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	new_str = _update_string_from_mode(update_msg->update_str,
					   update_msg->mode,
					   slurm_conf.suspend_exc_states,
					   true);

	if (!xstrcmp(new_str, slurm_conf.suspend_exc_states)) {
		info("SuspendExcStates did not change from %s with update: %s",
		      slurm_conf.suspend_exc_states, update_msg->update_str);
		xfree(new_str);
	} else {
		info("Setting SuspendExcStates to '%s'", new_str);
		_set_power_save_settings(new_str, &slurm_conf.suspend_exc_states);
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_set_schedlog_level(slurm_msg_t *msg)
{
	int schedlog_level;
	slurmctld_lock_t config_read_lock =
		{ READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	set_debug_level_msg_t *request_msg = msg->data;
	log_options_t log_opts = SCHEDLOG_OPTS_INITIALIZER;

	if (!validate_super_user(msg->auth_uid)) {
		error("set scheduler log level request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	/*
	 * If slurm_conf.sched_logfile is NULL, then this operation
	 *  will fail, since there is no sched logfile for which to alter
	 *  the log level. (Calling sched_log_alter with a NULL filename
	 *  is likely to cause a segfault at the next sched log call)
	 *  So just give up and return "Operation Disabled"
	 */
	if (slurm_conf.sched_logfile == NULL) {
		error("set scheduler log level failed: no log file!");
		slurm_send_rc_msg (msg, ESLURM_DISABLED);
		return;
	}

	schedlog_level = MIN (request_msg->debug_level, (LOG_LEVEL_QUIET + 1));
	schedlog_level = MAX (schedlog_level, LOG_LEVEL_QUIET);

	lock_slurmctld(config_read_lock);
	log_opts.logfile_level = schedlog_level;
	sched_log_alter(log_opts, LOG_DAEMON, slurm_conf.sched_logfile);

	sched_info("Set scheduler log level to %d", schedlog_level);

	slurm_conf.sched_log_level = schedlog_level;
	slurm_conf.last_update = time(NULL);
	unlock_slurmctld(config_read_lock);

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_accounting_update_msg(slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	accounting_update_msg_t *update_ptr = msg->data;
	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error("Update Association request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	if (!update_ptr->update_list || !list_count(update_ptr->update_list)) {
		slurm_send_rc_msg(msg, rc);
		return;
	}

	/*
	 * Before we send an rc we are transferring the update_list to a common
	 * list to avoid the potential of messages from the dbd getting out of
	 * order. The list lock here should protect us here as we only access
	 * this list in list_transfer and list_delete_all.
	 */
	xassert(slurmctld_config.acct_update_list);
	list_transfer(slurmctld_config.acct_update_list,
		      update_ptr->update_list);

	/*
	 * Send message back to the caller letting him know we got it.
	 * Since we have the update list in the order we got it we should be
	 * good to respond.  There should be no need to wait since the end
	 * result would be the same if we wait or not since the update has
	 * already happened in the database.
	 */
	slurm_send_rc_msg(msg, rc);

	/* Signal acct_update_thread to process list */
	slurm_mutex_lock(&slurmctld_config.acct_update_lock);
	slurm_cond_broadcast(&slurmctld_config.acct_update_cond);
	slurm_mutex_unlock(&slurmctld_config.acct_update_lock);

	END_TIMER2(__func__);

	if (rc != SLURM_SUCCESS)
		error("assoc_mgr_update gave error: %s",
		      slurm_strerror(rc));
}

/* _slurm_rpc_reboot_nodes - process RPC to schedule nodes reboot */
static void _slurm_rpc_reboot_nodes(slurm_msg_t *msg)
{
	int rc;
	char *err_msg = NULL;
	node_record_t *node_ptr;
	reboot_msg_t *reboot_msg = msg->data;
	char *nodelist = NULL;
	bitstr_t *bitmap = NULL, *cannot_reboot_nodes = NULL;
	/* Locks: write node lock */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	time_t now = time(NULL);
	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, REBOOT_NODES RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	/* do RPC call */
	if (reboot_msg)
		nodelist = reboot_msg->node_list;
	if (!nodelist || !xstrcasecmp(nodelist, "ALL")) {
		bitmap = node_conf_get_active_bitmap();
	} else {
		hostlist_t *hostlist;
		if (!(hostlist =
		      nodespec_to_hostlist(nodelist, true, NULL))) {
			error("%s: Bad node list in REBOOT_NODES request: \"%s\"",
			      __func__, nodelist);
			slurm_send_rc_msg(msg, ESLURM_INVALID_NODE_NAME);
			return;
		} else if ((rc = hostlist2bitmap(hostlist, true, &bitmap))) {
			error("%s: Can't find nodes requested in REBOOT_NODES request: \"%s\"",
			      __func__, nodelist);
			FREE_NULL_BITMAP(bitmap);
			FREE_NULL_HOSTLIST(hostlist);
			slurm_send_rc_msg(msg, ESLURM_INVALID_NODE_NAME);
			return;
		}
		FREE_NULL_HOSTLIST(hostlist);
	}

	cannot_reboot_nodes = bit_alloc(node_record_count);
	lock_slurmctld(node_write_lock);
	for (int i = 0; (node_ptr = next_node_bitmap(bitmap, &i)); i++) {
		if (IS_NODE_FUTURE(node_ptr) ||
		    IS_NODE_REBOOT_REQUESTED(node_ptr) ||
		    IS_NODE_REBOOT_ISSUED(node_ptr) ||
		    IS_NODE_POWER_DOWN(node_ptr) ||
		    IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_DOWN(node_ptr)) {
			bit_clear(bitmap, node_ptr->index);
			bit_set(cannot_reboot_nodes, node_ptr->index);
			debug2("Skipping reboot of node %s in state %s",
			       node_ptr->name,
			       node_state_string(node_ptr->node_state));
			continue;
		}
		node_ptr->node_state |= NODE_STATE_REBOOT_REQUESTED;
		if (reboot_msg) {
			node_ptr->next_state = reboot_msg->next_state;
			if (node_ptr->next_state == NODE_RESUME)
				bit_set(rs_node_bitmap, node_ptr->index);

			if (reboot_msg->reason) {
				xfree(node_ptr->reason);
				node_ptr->reason = xstrdup(reboot_msg->reason);
				node_ptr->reason_time = now;
				node_ptr->reason_uid = msg->auth_uid;
			}
			if (reboot_msg->flags & REBOOT_FLAGS_ASAP) {
				if (!IS_NODE_DRAIN(node_ptr)) {
					if (node_ptr->next_state == NO_VAL)
						node_ptr->next_state =
							NODE_STATE_UNDRAIN;
					else
						node_ptr->next_state |=
							NODE_STATE_UNDRAIN;
				}

				node_ptr->node_state |= NODE_STATE_DRAIN;
				bit_clear(avail_node_bitmap, node_ptr->index);
				bit_set(asap_node_bitmap, node_ptr->index);

				if (node_ptr->reason == NULL) {
					node_ptr->reason =
						xstrdup("Reboot ASAP");
					node_ptr->reason_time = now;
					node_ptr->reason_uid = msg->auth_uid;
				}
			}
			if (!node_ptr->reason) {
				node_ptr->reason = xstrdup("reboot requested");
				node_ptr->reason_time = now;
				node_ptr->reason_uid = msg->auth_uid;
			}
		}
		want_nodes_reboot = true;
	}

	if (want_nodes_reboot == true)
		schedule_node_save();
	unlock_slurmctld(node_write_lock);
	if (want_nodes_reboot == true) {
		nodelist = bitmap2node_name(bitmap);
		info("reboot request queued for nodes %s", nodelist);
		xfree(nodelist);
	}
	if (bit_ffs(cannot_reboot_nodes) != -1) {
		nodelist = bitmap2node_name(cannot_reboot_nodes);
		xstrfmtcat(err_msg, "Skipping reboot of nodes %s due to current node state.",
			   nodelist);
		xfree(nodelist);
	}
	FREE_NULL_BITMAP(cannot_reboot_nodes);
	FREE_NULL_BITMAP(bitmap);
	rc = SLURM_SUCCESS;
	END_TIMER2(__func__);
	slurm_send_rc_err_msg(msg, rc, err_msg);
	xfree(err_msg);
}

static void _slurm_rpc_accounting_first_reg(slurm_msg_t *msg)
{
	time_t event_time = time(NULL);

	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error("First Registration request from non-super user uid=%u",
		      msg->auth_uid);
		return;
	}

	acct_storage_g_send_all(acct_db_conn, event_time, ACCOUNTING_FIRST_REG);

	END_TIMER2(__func__);
}

static void _slurm_rpc_accounting_register_ctld(slurm_msg_t *msg)
{
	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error("Registration request from non-super user uid=%u",
		      msg->auth_uid);
		return;
	}

	clusteracct_storage_g_register_ctld(acct_db_conn,
	                                    slurm_conf.slurmctld_port);

	END_TIMER2(__func__);
}

static void _clear_rpc_stats(void)
{
	slurm_mutex_lock(&rpc_mutex);
	memset(rpc_type_cnt, 0, sizeof(rpc_type_cnt));
	memset(rpc_type_id, 0, sizeof(rpc_type_id));
	memset(rpc_type_time, 0, sizeof(rpc_type_time));
	memset(rpc_type_queued, 0, sizeof(rpc_type_queued));
	memset(rpc_type_dropped, 0, sizeof(rpc_type_dropped));
	memset(rpc_type_cycle_last, 0, sizeof(rpc_type_cycle_last));
	memset(rpc_type_cycle_max, 0, sizeof(rpc_type_cycle_max));
	memset(rpc_user_cnt, 0, sizeof(rpc_user_cnt));
	memset(rpc_user_id, 0, sizeof(rpc_user_id));
	memset(rpc_user_time, 0, sizeof(rpc_user_time));
	slurm_mutex_unlock(&rpc_mutex);
}

static void _pack_rpc_stats(buf_t *buffer, uint16_t protocol_version)
{
	slurm_mutex_lock(&rpc_mutex);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t rpc_count = 0, user_count = 1;
		uint8_t queue_enabled = rpc_queue_enabled();

		while (rpc_type_id[rpc_count])
			rpc_count++;
		pack32(rpc_count, buffer);
		pack16_array(rpc_type_id, rpc_count, buffer);
		pack32_array(rpc_type_cnt, rpc_count, buffer);
		pack64_array(rpc_type_time, rpc_count, buffer);

		pack8(queue_enabled, buffer);
		if (queue_enabled) {
			pack16_array(rpc_type_queued, rpc_count, buffer);
			pack64_array(rpc_type_dropped, rpc_count, buffer);
			pack16_array(rpc_type_cycle_last, rpc_count, buffer);
			pack16_array(rpc_type_cycle_max, rpc_count, buffer);
		}

		/* user_count starts at 1 as root is in index 0 */
		while (rpc_user_id[user_count])
			user_count++;
		pack32(user_count, buffer);
		pack32_array(rpc_user_id, user_count, buffer);
		pack32_array(rpc_user_cnt, user_count, buffer);
		pack64_array(rpc_user_time, user_count, buffer);

		agent_pack_pending_rpc_stats(buffer);
	}

	slurm_mutex_unlock(&rpc_mutex);
}

static void _slurm_rpc_burst_buffer_status(slurm_msg_t *msg)
{
	bb_status_resp_msg_t status_resp_msg;
	bb_status_req_msg_t *status_req_msg = msg->data;

	memset(&status_resp_msg, 0, sizeof(status_resp_msg));
	status_resp_msg.status_resp = bb_g_get_status(status_req_msg->argc,
						      status_req_msg->argv,
						      msg->auth_uid,
						      msg->auth_gid);
	(void) send_msg_response(msg, RESPONSE_BURST_BUFFER_STATUS,
				 &status_resp_msg);
	xfree(status_resp_msg.status_resp);
}

/* _slurm_rpc_dump_stats - process RPC for statistics information */
static void _slurm_rpc_dump_stats(slurm_msg_t *msg)
{
	stats_info_request_msg_t *request_msg = msg->data;
	buf_t *buffer = NULL;

	if ((request_msg->command_id == STAT_COMMAND_RESET) &&
	    !validate_operator(msg->auth_uid)) {
		error("Security violation: REQUEST_STATS_INFO reset from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	debug3("Processing RPC details: REQUEST_STATS_INFO command=%u",
	       request_msg->command_id);

	if (request_msg->command_id == STAT_COMMAND_RESET) {
		reset_stats(1);
		_clear_rpc_stats();
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		return;
	}

	buffer = pack_all_stat(msg->protocol_version);
	_pack_rpc_stats(buffer, msg->protocol_version);


	/* send message */
	(void) send_msg_response(msg, RESPONSE_STATS_INFO, buffer);
	FREE_NULL_BUFFER(buffer);
}

static void _slurm_rpc_dump_licenses(slurm_msg_t *msg)
{
	DEF_TIMERS;
	license_info_request_msg_t *lic_req_msg = msg->data;
	buf_t *buffer = NULL;

	START_TIMER;
	if ((lic_req_msg->last_update - 1) >= last_license_update) {
		/* Don't send unnecessary data
		 */
		debug2("%s: no change SLURM_NO_CHANGE_IN_DATA", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);

		return;
	}

	buffer = get_all_license_info(msg->protocol_version);
	END_TIMER2(__func__);

	(void) send_msg_response(msg, RESPONSE_LICENSE_INFO, buffer);
	FREE_NULL_BUFFER(buffer);
}

static void _slurm_rpc_kill_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	DEF_TIMERS;
	job_step_kill_msg_t *kill = msg->data;
	slurmctld_lock_t fed_job_read_lock =
		{NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t lock = {READ_LOCK, WRITE_LOCK,
				 WRITE_LOCK, NO_LOCK, READ_LOCK };
	int cc;

	/*
	 * If the cluster is part of a federation and it isn't the origin of the
	 * job then if it doesn't know about the federated job, then route the
	 * request to the origin cluster via the client. If the cluster does
	 * know about the job and it owns the job, then this cluster will cancel
	 * the job and it will report the cancel back to the origin. If job does
	 * reside on this cluster but doesn't own it (e.g. pending jobs), then
	 * route the request back to the origin to handle it.
	 */
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(fed_job_read_lock);
	if (fed_mgr_fed_rec) {
		uint32_t job_id, origin_id;
		job_record_t *job_ptr;
		slurmdb_cluster_rec_t *origin;

		job_id    = strtol(kill->sjob_id, NULL, 10);
		origin_id = fed_mgr_get_cluster_id(job_id);
		origin    = fed_mgr_get_cluster_by_id(origin_id);

		/*
		 * only reroute to the origin if the connection is up. If it
		 * isn't then _signal_job will signal the sibling jobs
		 */
		if (origin && origin->fed.send &&
		    ((persist_conn_t *) origin->fed.send)->conn &&
		    (origin != fed_mgr_cluster_rec) &&
		    (!(job_ptr = find_job_record(job_id)) ||
		     (job_ptr && job_ptr->fed_details &&
		      (job_ptr->fed_details->cluster_lock !=
		       fed_mgr_cluster_rec->fed.id)))) {

			slurmdb_cluster_rec_t *dst =
				fed_mgr_get_cluster_by_id(origin_id);
			if (!dst) {
				error("couldn't find cluster by cluster id %d",
				      origin_id);
				slurm_send_rc_msg(msg, SLURM_ERROR);
			} else {
				slurm_send_reroute_msg(msg, dst, NULL);
				info("%s: REQUEST_KILL_JOB JobId=%s uid %u routed to %s",
				     __func__, kill->sjob_id, msg->auth_uid,
				     dst->name);
			}
			if (!(msg->flags & CTLD_QUEUE_PROCESSING))
				unlock_slurmctld(fed_job_read_lock);
			return;
		}
	}
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(fed_job_read_lock);

	START_TIMER;
	info("%s: REQUEST_KILL_JOB JobId=%s uid %u",
	     __func__, kill->sjob_id, msg->auth_uid);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(lock);
	}
	if (kill->sibling) {
		uint32_t job_id = strtol(kill->sjob_id, NULL, 10);
		cc = fed_mgr_remove_active_sibling(job_id, kill->sibling);
	} else {
		cc = job_str_signal(kill->sjob_id, kill->signal, kill->flags,
				    msg->auth_uid, 0);
	}
	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(lock);
		_throttle_fini(&active_rpc_cnt);
	}

	if (cc == ESLURM_ALREADY_DONE) {
		debug2("%s: job_str_signal() uid=%u JobId=%s sig=%d returned: %s",
		       __func__, msg->auth_uid, kill->sjob_id,
		       kill->signal, slurm_strerror(cc));
	} else if (cc != SLURM_SUCCESS) {
		info("%s: job_str_signal() uid=%u JobId=%s sig=%d returned: %s",
		     __func__, msg->auth_uid, kill->sjob_id,
		     kill->signal, slurm_strerror(cc));
	} else {
		slurmctld_diag_stats.jobs_canceled++;
	}

	slurm_send_rc_msg(msg, cc);

	END_TIMER2(__func__);
}

static char *_str_array2str(char **array, uint32_t cnt)
{
	char *ret_str = NULL;
	char *pos = NULL;

	for (int i = 0; i < cnt; i++) {
		if (!pos) /* First string */
			xstrfmtcatat(ret_str, &pos, "%s", array[i]);
		else
			xstrfmtcatat(ret_str, &pos, ",%s", array[i]);
	}
	return ret_str;
}

static void _log_kill_jobs_rpc(kill_jobs_msg_t *kill_msg)
{
	char *job_ids_str = _str_array2str(kill_msg->jobs_array,
					   kill_msg->jobs_cnt);

	verbose("%s filters: account=%s; flags=0x%x; job_name=%s; partition=%s; qos=%s; reservation=%s; signal=%u; state=%d(%s); user_id=%u, user_name=%s; wckey=%s; nodelist=%s; jobs=%s",
		rpc_num2string(REQUEST_KILL_JOBS), kill_msg->account,
		kill_msg->flags, kill_msg->job_name, kill_msg->partition,
		kill_msg->qos, kill_msg->reservation, kill_msg->signal,
		kill_msg->state,
		kill_msg->state ? job_state_string(kill_msg->state) : "none",
		kill_msg->user_id, kill_msg->user_name, kill_msg->wckey,
		kill_msg->nodelist, job_ids_str);
	xfree(job_ids_str);
}

static void _slurm_rpc_kill_jobs(slurm_msg_t *msg)
{
	int rc;
	DEF_TIMERS;
	kill_jobs_msg_t *kill_msg = msg->data;
	kill_jobs_resp_msg_t *kill_msg_resp = NULL;
	slurmctld_lock_t lock = {
		.conf = READ_LOCK,
		.job = WRITE_LOCK,
		.node = WRITE_LOCK,
		.fed = READ_LOCK,
	};

	if ((slurm_conf.debug_flags & DEBUG_FLAG_PROTOCOL) ||
	    (slurm_conf.slurmctld_debug >= LOG_LEVEL_DEBUG2))
		_log_kill_jobs_rpc(kill_msg);

	if (!validate_super_user(msg->auth_uid) && kill_msg->admin_comment) {
		error("%s: attempt to set AdminComment by %u",
		      __func__, msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		lock_slurmctld(lock);
	}
	rc = job_mgr_signal_jobs(kill_msg, msg->auth_uid, &kill_msg_resp);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(lock);
	}
	END_TIMER2(__func__);

	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		(void) send_msg_response(msg, RESPONSE_KILL_JOBS,
					 kill_msg_resp);
	}

	slurm_free_kill_jobs_response_msg(kill_msg_resp);
}

/* _slurm_rpc_assoc_mgr_info()
 *
 * Pack the assoc_mgr lists and return it back to the caller.
 */
static void _slurm_rpc_assoc_mgr_info(slurm_msg_t *msg)
{
	DEF_TIMERS;
	buf_t *buffer;

	START_TIMER;
	/* Security is handled in the assoc_mgr */
	buffer = assoc_mgr_info_get_pack_msg(msg->data, msg->auth_uid,
					     acct_db_conn,
					     msg->protocol_version);
	END_TIMER2(__func__);

	if (!buffer) {
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	(void) send_msg_response(msg, RESPONSE_ASSOC_MGR_INFO, buffer);
	FREE_NULL_BUFFER(buffer);
}

/* Take a persist_msg_t and handle it like a normal slurm_msg_t */
static int _process_persist_conn(void *arg, persist_msg_t *persist_msg,
				 buf_t **out_buffer)
{
	slurm_msg_t msg;
	slurmctld_rpc_t *this_rpc = NULL;
	persist_conn_t *persist_conn = arg;

	*out_buffer = NULL;

	slurm_msg_t_init(&msg);

	msg.auth_cred = persist_conn->auth_cred;
	msg.auth_uid = persist_conn->auth_uid;
	msg.auth_gid = persist_conn->auth_gid;
	msg.auth_ids_set = persist_conn->auth_ids_set;

	msg.pcon = persist_conn;
	msg.conn = persist_conn->conn;

	msg.msg_type = persist_msg->msg_type;
	msg.data = persist_msg->data;
	msg.protocol_version = persist_conn->version;

	if (persist_conn->persist_type == PERSIST_TYPE_ACCT_UPDATE) {
		if (msg.msg_type == ACCOUNTING_UPDATE_MSG) {
			DEF_TIMERS;
			START_TIMER;
			_slurm_rpc_accounting_update_msg(&msg);
			END_TIMER;
			record_rpc_stats(&msg, DELTA_TIMER);
		} else {
			slurm_send_rc_msg(&msg, EINVAL);
		}
	} else if ((this_rpc = find_rpc(persist_msg->msg_type))) {
		xassert(!this_rpc->keep_msg);
		/* directly process the request */
		slurmctld_req(&msg, this_rpc);
	} else {
		error("invalid RPC msg_type=%s",
		      rpc_num2string(persist_msg->msg_type));
		slurm_send_rc_msg(&msg, EINVAL);
	}

	return SLURM_SUCCESS;
}

static void _slurm_rpc_persist_init(slurm_msg_t *msg)
{
	DEF_TIMERS;
	int rc = SLURM_SUCCESS, fd = -1;
	char *comment = NULL;
	buf_t *ret_buf;
	persist_conn_t *persist_conn = NULL, p_tmp = { 0 };
	persist_init_req_msg_t *persist_init = msg->data;
	slurm_addr_t rem_addr;

	if (msg->pcon)
		error("We already have a persistent connect, this should never happen");

	START_TIMER;

	if (persist_init->version > SLURM_PROTOCOL_VERSION)
		persist_init->version = SLURM_PROTOCOL_VERSION;

	p_tmp.cluster_name = persist_init->cluster_name;
	p_tmp.version = persist_init->version;
	p_tmp.shutdown = &slurmctld_config.shutdown_time;

	if (!validate_slurm_user(msg->auth_uid)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_PERSIST_INIT RPC from uid=%u",
		      msg->auth_uid);
		goto end_it;
	}

	if ((fd = conn_g_get_fd(msg->conn)) < 0) {
		rc = EBADF;
		goto end_it;
	}

	/*
	 * Persistent connection handlers expect file descriptor to be already
	 * configured as nonblocking with keepalive activated
	 */
	fd_set_nonblocking(fd);
	net_set_keep_alive(fd);

	persist_conn = xmalloc(sizeof(persist_conn_t));

	persist_conn->auth_uid = msg->auth_uid;
	persist_conn->auth_gid = msg->auth_gid;
	persist_conn->auth_ids_set = msg->auth_ids_set;

	persist_conn->auth_cred = msg->auth_cred;
	msg->auth_cred = NULL;

	persist_conn->cluster_name = persist_init->cluster_name;
	persist_init->cluster_name = NULL;

	persist_conn->conn = msg->conn;
	msg->conn = NULL;

	persist_conn->callback_proc = _process_persist_conn;

	persist_conn->persist_type = persist_init->persist_type;
	persist_conn->rem_port = persist_init->port;

	persist_conn->rem_host = xmalloc(INET6_ADDRSTRLEN);
	(void) slurm_get_peer_addr(conn_g_get_fd(persist_conn->conn),
				   &rem_addr);
	slurm_get_ip_str(&rem_addr, persist_conn->rem_host, INET6_ADDRSTRLEN);

	/* info("got it from %d %s %s(%u)", persist_conn->fd, */
	/*      persist_conn->cluster_name, */
	/*      persist_conn->rem_host, persist_conn->rem_port); */
	persist_conn->shutdown = &slurmctld_config.shutdown_time;
	//persist_conn->timeout = 0; /* we want this to be 0 */

	persist_conn->version = persist_init->version;

	memcpy(&p_tmp, persist_conn, sizeof(persist_conn_t));

	if (persist_init->persist_type == PERSIST_TYPE_FED)
		rc = fed_mgr_add_sibling_conn(persist_conn, &comment);
	else if (persist_init->persist_type == PERSIST_TYPE_ACCT_UPDATE) {
		persist_conn->flags |= PERSIST_FLAG_ALREADY_INITED;

		slurm_persist_conn_recv_thread_init(
			persist_conn, conn_g_get_fd(persist_conn->conn), -1,
			persist_conn);
	} else
		rc = SLURM_ERROR;
end_it:

	/* If people are really hammering the fed_mgr we could get into trouble
	 * with the persist_conn we sent in, so use the copy instead
	 */
	ret_buf = slurm_persist_make_rc_msg(&p_tmp, rc, comment, p_tmp.version);
	if (slurm_persist_send_msg(&p_tmp, ret_buf) != SLURM_SUCCESS) {
		debug("Problem sending response to connection %d uid(%u)",
		      conn_g_get_fd(p_tmp.conn), msg->auth_uid);
	}

	if (rc && persist_conn) {
		/* Free AFTER message has been sent back to remote */
		persist_conn->conn = NULL;
		slurm_persist_conn_destroy(persist_conn);
	}
	xfree(comment);
	FREE_NULL_BUFFER(ret_buf);
	END_TIMER;

	/* Don't free this here, it will be done elsewhere */
	//slurm_persist_conn_destroy(persist_conn);
}

static void _slurm_rpc_tls_cert(slurm_msg_t *msg)
{
	tls_cert_request_msg_t *req = msg->data;
	tls_cert_response_msg_t resp = { 0 };
	node_record_t *node = NULL;
	bool is_client_auth = false;

	if (!validate_slurm_user(msg->auth_uid)) {
		error("Security violation, REQUEST_TLS_CERT from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	if (!(node = find_node_record(req->node_name))) {
		log_flag(TLS, "%s: Could not find node record. Request might not be from a slurmd node",
			 __func__);
	}

	is_client_auth = conn_g_is_client_authenticated(msg->conn);

	if (!(resp.signed_cert =
		      certmgr_g_sign_csr(req->csr, is_client_auth, req->token,
					 req->node_name))) {
		error("%s: Unable to sign certificate signing request.",
		      __func__);
		slurm_send_rc_msg(msg, SLURM_ERROR);
	} else if (node) {
		node->cert_last_renewal = time(NULL);
	}

	if (resp.signed_cert) {
		log_flag(AUDIT_TLS, "Sending signed certificate back to node \'%s\'",
			 req->node_name);
	}

	(void) send_msg_response(msg, RESPONSE_TLS_CERT, &resp);
	slurm_free_tls_cert_response_msg_members(&resp);
}

static void _slurm_rpc_sib_job_lock(slurm_msg_t *msg)
{
	int rc;
	sib_msg_t *sib_msg = msg->data;

	if (!msg->pcon) {
		error("Security violation, SIB_JOB_LOCK RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	rc = fed_mgr_job_lock_set(sib_msg->step_id.job_id, sib_msg->cluster_id);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_sib_job_unlock(slurm_msg_t *msg)
{
	int rc;
	sib_msg_t *sib_msg = msg->data;

	if (!msg->pcon) {
		error("Security violation, SIB_JOB_UNLOCK RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	rc = fed_mgr_job_lock_unset(sib_msg->step_id.job_id,
				    sib_msg->cluster_id);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_sib_msg(uint32_t uid, slurm_msg_t *msg) {
	if (!msg->pcon) {
		error("Security violation, SIB_SUBMISSION RPC from uid=%u",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	fed_mgr_q_sib_msg(msg, uid);
}

static void _slurm_rpc_dependency_msg(uint32_t uid, slurm_msg_t *msg)
{
	if (!msg->pcon || !validate_slurm_user(uid)) {
		error("Security violation, REQUEST_SEND_DEP RPC from uid=%u",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	fed_mgr_q_dep_msg(msg);
}

static void _slurm_rpc_update_origin_dep_msg(uint32_t uid, slurm_msg_t *msg)
{
	if (!msg->pcon || !validate_slurm_user(uid)) {
		error("Security violation, REQUEST_UPDATE_ORIGIN_DEP RPC from uid=%u",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	fed_mgr_q_update_origin_dep_msg(msg);
}

static buf_t *_build_rc_buf(int rc, uint16_t rpc_version)
{
	buf_t *buf = NULL;
	slurm_msg_t msg;
	return_code_msg_t data;

	memset(&data, 0, sizeof(data));
	data.return_code = rc;
	slurm_msg_t_init(&msg);
	msg.msg_type = RESPONSE_SLURM_RC;
	msg.data = &data;
	buf = init_buf(128);
	pack16(msg.msg_type, buf);
	if (pack_msg(&msg, buf) != SLURM_SUCCESS)
		FREE_NULL_BUFFER(buf);

	return buf;
}

/* Free buf_t *record from a list */
static void _ctld_free_list_msg(void *x)
{
	FREE_NULL_BUFFER(x);
}

static int _foreach_proc_multi_msg(void *x, void *arg)
{
	buf_t *single_req_buf = x;
	foreach_multi_msg_t *multi_msg = arg;
	slurm_msg_t *msg = multi_msg->msg;
	slurm_msg_t sub_msg;
	buf_t *ret_buf;

	slurm_msg_t_init(&sub_msg);
	sub_msg.protocol_version = msg->protocol_version;
	if (unpack16(&sub_msg.msg_type, single_req_buf) ||
	    unpack_msg(&sub_msg, single_req_buf)) {
		error("Sub-message unpack error for REQUEST_CTLD_MULT_MSG %u RPC",
		      sub_msg.msg_type);
		ret_buf = _build_rc_buf(SLURM_ERROR, msg->protocol_version);
		list_append(multi_msg->full_resp_list, ret_buf);
		return 0;
	}
	sub_msg.pcon = msg->pcon;
	sub_msg.auth_cred = msg->auth_cred;
	ret_buf = NULL;

	log_flag(PROTOCOL, "%s: received opcode %s",
		 __func__, rpc_num2string(sub_msg.msg_type));

	switch (sub_msg.msg_type) {
	case REQUEST_PING:
		ret_buf = _build_rc_buf(SLURM_SUCCESS, msg->protocol_version);
		break;
	case REQUEST_SIB_MSG:
		_slurm_rpc_sib_msg(msg->auth_uid, &sub_msg);
		ret_buf = _build_rc_buf(SLURM_SUCCESS, msg->protocol_version);
		break;
	case REQUEST_SEND_DEP:
		_slurm_rpc_dependency_msg(msg->auth_uid, &sub_msg);
		ret_buf = _build_rc_buf(SLURM_SUCCESS, msg->protocol_version);
		break;
	case REQUEST_UPDATE_ORIGIN_DEP:
		_slurm_rpc_update_origin_dep_msg(msg->auth_uid, &sub_msg);
		ret_buf = _build_rc_buf(SLURM_SUCCESS, msg->protocol_version);
		break;
	default:
		error("%s: Unsupported Message Type:%s",
		      __func__, rpc_num2string(sub_msg.msg_type));
	}
	(void) slurm_free_msg_data(sub_msg.msg_type, sub_msg.data);

	if (!ret_buf)
		ret_buf = _build_rc_buf(SLURM_ERROR, msg->protocol_version);

	list_append(multi_msg->full_resp_list, ret_buf);

	return 0;
}

static void _proc_multi_msg(slurm_msg_t *msg)
{
	ctld_list_msg_t *ctld_req_msg = msg->data;
	ctld_list_msg_t ctld_resp_msg = { 0 };
	foreach_multi_msg_t multi_msg = {
		.msg = msg,
	};

	if (!msg->pcon) {
		error("Security violation, REQUEST_CTLD_MULT_MSG RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	multi_msg.full_resp_list = list_create(_ctld_free_list_msg);
	(void) list_for_each(ctld_req_msg->my_list,
			     _foreach_proc_multi_msg,
			     &multi_msg);

	ctld_resp_msg.my_list = multi_msg.full_resp_list;

	/* Send message */
	(void) send_msg_response(msg, RESPONSE_CTLD_MULT_MSG, &ctld_resp_msg);
	FREE_NULL_LIST(multi_msg.full_resp_list);
}

/* Route msg to federated job's origin.
 * RET returns SLURM_SUCCESS if the msg was routed.
 */
static int _route_msg_to_origin(slurm_msg_t *msg, char *src_job_id_str,
				uint32_t src_job_id)
{
	xassert(msg);

	/* route msg to origin cluster if a federated job */
	if (!msg->pcon && fed_mgr_fed_rec) {
		/* Don't send reroute if coming from a federated cluster (aka
		 * has a msg->pcon). */
		uint32_t job_id, origin_id;

		if (src_job_id_str)
			job_id = strtol(src_job_id_str, NULL, 10);
		else
			job_id = src_job_id;
		origin_id = fed_mgr_get_cluster_id(job_id);

		if (origin_id && (origin_id != fed_mgr_cluster_rec->fed.id)) {
			slurmdb_cluster_rec_t *dst =
				fed_mgr_get_cluster_by_id(origin_id);
			if (!dst) {
				error("couldn't find cluster by cluster id %d",
				      origin_id);
				slurm_send_rc_msg(msg, SLURM_ERROR);
			} else {
				slurm_send_reroute_msg(msg, dst, NULL);
				info("%s: %s JobId=%u uid %u routed to %s",
				     __func__, rpc_num2string(msg->msg_type),
				     job_id, msg->auth_uid, dst->name);
			}

			return SLURM_SUCCESS;
		}
	}

	return SLURM_ERROR;
}

static void _slurm_rpc_set_fs_dampening_factor(slurm_msg_t *msg)
{
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	set_fs_dampening_factor_msg_t *request_msg = msg->data;
	uint16_t factor;

	if (!validate_super_user(msg->auth_uid)) {
		error("set FairShareDampeningFactor request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}
	factor = request_msg->dampening_factor;

	lock_slurmctld(config_write_lock);
	slurm_conf.fs_dampening_factor = factor;
	slurm_conf.last_update = time(NULL);
	priority_g_reconfig(false);
	unlock_slurmctld(config_write_lock);

	info("Set FairShareDampeningFactor to %u", factor);
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_request_crontab(slurm_msg_t *msg)
{
	DEF_TIMERS;
	int rc = SLURM_SUCCESS;
	crontab_request_msg_t *req_msg = msg->data;
	buf_t *crontab = NULL;
	char *disabled_lines = NULL;
	slurmctld_lock_t job_read_lock = { .job = READ_LOCK };

	START_TIMER;
	debug3("Processing RPC details: REQUEST_CRONTAB for uid=%u",
	       req_msg->uid);

	if (!xstrcasestr(slurm_conf.scron_params, "enable")) {
		error("%s: scrontab is disabled on this cluster", __func__);
		slurm_send_rc_msg(msg, SLURM_ERROR);
		return;
	}

	lock_slurmctld(job_read_lock);

	if ((req_msg->uid != msg->auth_uid) &&
	    !validate_operator(msg->auth_uid)) {
		rc = ESLURM_USER_ID_MISSING;
	} else {
		char *file = NULL;
		xstrfmtcat(file, "%s/crontab/crontab.%u",
			   slurm_conf.state_save_location, req_msg->uid);

		if (!(crontab = create_mmap_buf(file)))
			rc = ESLURM_JOB_SCRIPT_MISSING;
		else {
			int len = strlen(crontab->head) + 1;
			disabled_lines = xstrndup(crontab->head + len,
						  crontab->size - len);
			/*
			 * Remove extra trailing command which would be
			 * parsed as an extraneous 0.
			 */
			if (disabled_lines) {
				len = strlen(disabled_lines) - 1;
				disabled_lines[len] = '\0';
			}
		}
		xfree(file);
	}

	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		crontab_response_msg_t resp_msg = {
			.crontab = crontab->head,
			.disabled_lines = disabled_lines,
		};
		(void) send_msg_response(msg, RESPONSE_CRONTAB, &resp_msg);
		FREE_NULL_BUFFER(crontab);
		xfree(disabled_lines);
	}
}

static void _slurm_rpc_update_crontab(slurm_msg_t *msg)
{
	DEF_TIMERS;
	crontab_update_request_msg_t *req_msg = msg->data;
	crontab_update_response_msg_t *resp_msg;
	/* probably need to mirror _slurm_rpc_dump_batch_script() */
	slurmctld_lock_t job_write_lock =
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	debug3("Processing RPC details: REQUEST_UPDATE_CRONTAB for uid=%u",
	       req_msg->uid);

	if (!xstrcasestr(slurm_conf.scron_params, "enable")) {
		error("%s: scrontab is disabled on this cluster", __func__);
		slurm_send_rc_msg(msg, SLURM_ERROR);
		return;
	}

	resp_msg = xmalloc(sizeof(*resp_msg));
	resp_msg->err_msg = NULL;
	resp_msg->job_submit_user_msg = NULL;
	resp_msg->failed_lines = NULL;
	resp_msg->return_code = SLURM_SUCCESS;

	lock_slurmctld(job_write_lock);

	if (((req_msg->uid != msg->auth_uid) ||
	     (req_msg->gid != msg->auth_gid)) &&
	    !validate_slurm_user(msg->auth_uid)) {
		resp_msg->return_code = ESLURM_USER_ID_MISSING;
	}

	if (!resp_msg->return_code) {
		char *alloc_node = NULL;
		void *id = NULL;
		_set_hostname(msg, &alloc_node);
		_set_identity(msg, &id);
		if (!alloc_node || (alloc_node[0] == '\0'))
			resp_msg->return_code = ESLURM_INVALID_NODE_NAME;
		else
			crontab_submit(req_msg, resp_msg, alloc_node, id,
				       msg->protocol_version);
		xfree(alloc_node);
		FREE_NULL_IDENTITY(id);
	}

	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	(void) send_msg_response(msg, RESPONSE_UPDATE_CRONTAB, resp_msg);

	slurm_free_crontab_update_response_msg(resp_msg);
}

static void _slurm_rpc_node_alias_addrs(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *node_list = ((slurm_node_alias_addrs_t *)msg->data)->node_list;
	slurm_node_alias_addrs_t alias_addrs = {0};
	hostlist_t *hl;
	bitstr_t *node_bitmap = NULL;
	node_record_t *node_ptr;

	slurmctld_lock_t node_read_lock = {
		.node = READ_LOCK,
	};

	START_TIMER;
	debug3("Processing RPC details: REQUEST_NODE_ALIAS_ADDRS");

	lock_slurmctld(node_read_lock);

	if (!(hl = hostlist_create(node_list))) {
		error("hostlist_create error for %s: %m",
		      node_list);
		goto end_it;
	}

	hostlist2bitmap(hl, true, &node_bitmap);
	FREE_NULL_HOSTLIST(hl);

	if (bit_ffs(node_bitmap) != -1) {
		int addr_index = 0;
		alias_addrs.node_list = bitmap2node_name_sortable(node_bitmap,
								  false);
		alias_addrs.node_cnt = bit_set_count(node_bitmap);
		alias_addrs.node_addrs = xcalloc(alias_addrs.node_cnt,
						 sizeof(slurm_addr_t));
		for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i));
		     i++) {
			slurm_conf_get_addr(
				node_ptr->name,
				&alias_addrs.node_addrs[addr_index++], 0);
		}
	}

end_it:
	unlock_slurmctld(node_read_lock);
	END_TIMER2(__func__);

	if (alias_addrs.node_addrs) {
		(void) send_msg_response(msg, RESPONSE_NODE_ALIAS_ADDRS, &alias_addrs);
	} else {
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	}

	xfree(alias_addrs.node_addrs);
	xfree(alias_addrs.node_list);
	FREE_NULL_BITMAP(node_bitmap);
}

static void _slurm_rpc_dbd_relay(slurm_msg_t *msg)
{
	DEF_TIMERS;
	int rc;
	persist_msg_t *persist_msg = msg->data;

	START_TIMER;
	debug3("Processing RPC details: REQUEST_DBD_RELAY");

	if (!validate_slurmd_user(msg->auth_uid)) {
		error("Security violation, %s RPC from uid=%u",
		      rpc_num2string(msg->msg_type), msg->auth_uid);
		return;
	}

	rc = acct_storage_g_relay_msg(acct_db_conn, persist_msg);

	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

slurmctld_rpc_t slurmctld_rpcs[] =
{
	{
		.msg_type = REQUEST_RESOURCE_ALLOCATION,
		.func = _slurm_rpc_allocate_resources,
	},{
		.msg_type = REQUEST_HET_JOB_ALLOCATION,
		.func = _slurm_rpc_allocate_het_job,
	},{
		.msg_type = REQUEST_BUILD_INFO,
		.func = _slurm_rpc_dump_conf,
	},{
		.msg_type = REQUEST_JOB_INFO,
		.func = _slurm_rpc_dump_jobs,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = READ_LOCK,
			.part = READ_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_STATE,
		.func = _slurm_rpc_job_state,
	},{
		.msg_type = REQUEST_JOB_USER_INFO,
		.func = _slurm_rpc_dump_jobs_user,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = READ_LOCK,
			.part = READ_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_INFO_SINGLE,
		.func = _slurm_rpc_dump_job_single,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = READ_LOCK,
			.part = READ_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_HOSTLIST_EXPANSION,
		.func = _slurm_rpc_hostlist_expansion,
		.queue_enabled = true,
		.locks = {
			.node = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_BATCH_SCRIPT,
		.func = _slurm_rpc_dump_batch_script,
	},{
		.msg_type = REQUEST_SHARE_INFO,
		.func = _slurm_rpc_get_shares,
	},{
		.msg_type = REQUEST_PRIORITY_FACTORS,
		.func = _slurm_rpc_get_priority_factors,
	},{
		.msg_type = REQUEST_JOB_END_TIME,
		.func = _slurm_rpc_end_time,
	},{
		.msg_type = REQUEST_FED_INFO,
		.func = _slurm_rpc_get_fed,
		.queue_enabled = true,
		.locks = {
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_NODE_INFO,
		.func = _slurm_rpc_dump_nodes,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.node = WRITE_LOCK,
			.part = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_NODE_INFO_SINGLE,
		.func = _slurm_rpc_dump_node_single,
	},{
		.msg_type = REQUEST_PARTITION_INFO,
		.func = _slurm_rpc_dump_partitions,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.part = READ_LOCK,
		},
	},{
		.msg_type = MESSAGE_EPILOG_COMPLETE,
		.max_per_cycle = 256,
		.func = _slurm_rpc_epilog_complete,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
		},
	},{
		.msg_type = REQUEST_CANCEL_JOB_STEP,
		.func = _slurm_rpc_job_step_kill,
	},{
		.msg_type = REQUEST_COMPLETE_JOB_ALLOCATION,
		.func = _slurm_rpc_complete_job_allocation,
	},{
		.msg_type = REQUEST_COMPLETE_PROLOG,
		.func = _slurm_rpc_complete_prolog,
		.queue_enabled = true,
		.locks = {
			.job = WRITE_LOCK,
		},
	},{
		.msg_type = REQUEST_COMPLETE_BATCH_SCRIPT,
		.max_per_cycle = 256,
		.func = _slurm_rpc_complete_batch_script,
		.queue_enabled = true,
		.locks = {
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_STEP_CREATE,
		.func = _slurm_rpc_job_step_create,
		.skip_stale = true,
		.queue_enabled = true,
		.locks = {
			.job = WRITE_LOCK,
			.node = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_STEP_INFO,
		.func = _slurm_rpc_job_step_get_info,
	},{
		.msg_type = REQUEST_JOB_WILL_RUN,
		.func = _slurm_rpc_job_will_run,
	},{
		.msg_type = REQUEST_SIB_JOB_LOCK,
		.func = _slurm_rpc_sib_job_lock,
	},{
		.msg_type = REQUEST_SIB_JOB_UNLOCK,
		.func = _slurm_rpc_sib_job_unlock,
	},{
		.msg_type = REQUEST_CTLD_MULT_MSG,
		.func = _proc_multi_msg,
	},{
		.msg_type = MESSAGE_NODE_REGISTRATION_STATUS,
		.func = _slurm_rpc_node_registration,
		.post_func = _slurm_post_rpc_node_registration,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.part = WRITE_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_ALLOCATION_INFO,
		.func = _slurm_rpc_job_alloc_info,
	},{
		.msg_type = REQUEST_HET_JOB_ALLOC_INFO,
		.func = _slurm_rpc_het_job_alloc_info,
		.skip_stale = true,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = READ_LOCK,
			.node = READ_LOCK,
			.part = NO_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_SBCAST_CRED,
		.func = _slurm_rpc_job_sbcast_cred,
	},{
		.msg_type = REQUEST_SBCAST_CRED_NO_JOB,
		.func = _slurm_rpc_sbcast_cred_no_job,
	},{
		.msg_type = REQUEST_PING,
		.func = _slurm_rpc_ping,
	},{
		.msg_type = REQUEST_RECONFIGURE,
		.func = _slurm_rpc_reconfigure_controller,
		.keep_msg = true,
	},{
		.msg_type = REQUEST_CONTROL,
		.func = _slurm_rpc_request_control,
	},{
		.msg_type = REQUEST_TAKEOVER,
		.func = _slurm_rpc_takeover,
	},{
		.msg_type = REQUEST_SHUTDOWN,
		.func = _slurm_rpc_shutdown_controller,
	},{
		.msg_type = REQUEST_SUBMIT_BATCH_JOB,
		.max_per_cycle = 256,
		.func = _slurm_rpc_submit_batch_job,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.part = READ_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_SUBMIT_BATCH_HET_JOB,
		.func = _slurm_rpc_submit_batch_het_job,
	},{
		.msg_type = REQUEST_UPDATE_JOB,
		.func = _slurm_rpc_update_job,
	},{
		.msg_type = REQUEST_CREATE_NODE,
		.func = _slurm_rpc_create_node,
	},{
		.msg_type = REQUEST_UPDATE_NODE,
		.func = _slurm_rpc_update_node,
	},{
		.msg_type = REQUEST_DELETE_NODE,
		.func = _slurm_rpc_delete_node,
	},{
		.msg_type = REQUEST_CREATE_PARTITION,
		.func = _slurm_rpc_update_partition,
	},{
		.msg_type = REQUEST_UPDATE_PARTITION,
		.func = _slurm_rpc_update_partition,
	},{
		.msg_type = REQUEST_DELETE_PARTITION,
		.func = _slurm_rpc_delete_partition,
	},{
		.msg_type = REQUEST_CREATE_RESERVATION,
		.func = _slurm_rpc_resv_create,
	},{
		.msg_type = REQUEST_UPDATE_RESERVATION,
		.func = _slurm_rpc_resv_update,
	},{
		.msg_type = REQUEST_DELETE_RESERVATION,
		.func = _slurm_rpc_resv_delete,
	},{
		.msg_type = REQUEST_RESERVATION_INFO,
		.func = _slurm_rpc_resv_show,
	},{
		.msg_type = REQUEST_NODE_REGISTRATION_STATUS,
		.func = _slurm_rpc_node_registration_status,
	},{
		.msg_type = REQUEST_SUSPEND,
		.func = _slurm_rpc_suspend,
	},{
		.msg_type = REQUEST_TOP_JOB,
		.func = _slurm_rpc_top_job,
	},{
		.msg_type = REQUEST_AUTH_TOKEN,
		.func = _slurm_rpc_auth_token,
	},{
		.msg_type = REQUEST_JOB_REQUEUE,
		.func = _slurm_rpc_requeue,
	},{
		.msg_type = REQUEST_JOB_READY,
		.func = _slurm_rpc_job_ready,
	},{
		.msg_type = REQUEST_BURST_BUFFER_INFO,
		.func = _slurm_rpc_burst_buffer_info,
	},{
		.msg_type = REQUEST_STEP_BY_CONTAINER_ID,
		.func = _slurm_rpc_step_by_container_id,
	},{
		.msg_type = REQUEST_STEP_COMPLETE,
		.max_per_cycle = 256,
		.func = _slurm_rpc_step_complete,
		.queue_enabled = true,
		.locks = {
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_STEP_LAYOUT,
		.func = _slurm_rpc_step_layout,
	},{
		.msg_type = REQUEST_UPDATE_JOB_STEP,
		.func = _slurm_rpc_step_update,
	},{
		.msg_type = REQUEST_CONFIG,
		.func = _slurm_rpc_config_request,
	},{
		.msg_type = REQUEST_TRIGGER_SET,
		.func = _slurm_rpc_trigger_set,
	},{
		.msg_type = REQUEST_TRIGGER_GET,
		.func = _slurm_rpc_trigger_get,
	},{
		.msg_type = REQUEST_TRIGGER_CLEAR,
		.func = _slurm_rpc_trigger_clear,
	},{
		.msg_type = REQUEST_TRIGGER_PULL,
		.func = _slurm_rpc_trigger_pull,
	},{
		.msg_type = REQUEST_JOB_NOTIFY,
		.func = _slurm_rpc_job_notify,
	},{
		.msg_type = REQUEST_SET_DEBUG_FLAGS,
		.func = _slurm_rpc_set_debug_flags,
	},{
		.msg_type = REQUEST_SET_DEBUG_LEVEL,
		.func = _slurm_rpc_set_debug_level,
	},{
		.msg_type = REQUEST_SET_SCHEDLOG_LEVEL,
		.func = _slurm_rpc_set_schedlog_level,
	},{
		.msg_type = REQUEST_SET_SUSPEND_EXC_NODES,
		.func = _slurm_rpc_set_suspend_exc_nodes,
	},{
		.msg_type = REQUEST_SET_SUSPEND_EXC_PARTS,
		.func = _slurm_rpc_set_suspend_exc_parts,
	},{
		.msg_type = REQUEST_SET_SUSPEND_EXC_STATES,
		.func = _slurm_rpc_set_suspend_exc_states,
	},{
		.msg_type = ACCOUNTING_UPDATE_MSG,
		.func = _slurm_rpc_accounting_update_msg,
	},{
		.msg_type = ACCOUNTING_FIRST_REG,
		.func = _slurm_rpc_accounting_first_reg,
	},{
		.msg_type = ACCOUNTING_REGISTER_CTLD,
		.func = _slurm_rpc_accounting_register_ctld,
	},{
		.msg_type = REQUEST_TOPO_CONFIG,
		.func = _slurm_rpc_get_topo_config,
	},{
		.msg_type = REQUEST_TOPO_INFO,
		.func = _slurm_rpc_get_topo,
	},{
		.msg_type = REQUEST_REBOOT_NODES,
		.func = _slurm_rpc_reboot_nodes,
	},{
		.msg_type = REQUEST_STATS_INFO,
		.func = _slurm_rpc_dump_stats,
	},{
		.msg_type = REQUEST_LICENSE_INFO,
		.func = _slurm_rpc_dump_licenses,
	},{
		.msg_type = REQUEST_KILL_JOB,
		.max_per_cycle = 256,
		.func = _slurm_rpc_kill_job,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_KILL_JOBS,
		.max_per_cycle = 256,
		.func = _slurm_rpc_kill_jobs,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_ASSOC_MGR_INFO,
		.func = _slurm_rpc_assoc_mgr_info,
	},{
		.msg_type = REQUEST_PERSIST_INIT,
		.func = _slurm_rpc_persist_init,
	},{
		.msg_type = REQUEST_SET_FS_DAMPENING_FACTOR,
		.func = _slurm_rpc_set_fs_dampening_factor,
	},{
		.msg_type = REQUEST_CONTROL_STATUS,
		.func = slurm_rpc_control_status,
	},{
		.msg_type = REQUEST_BURST_BUFFER_STATUS,
		.func = _slurm_rpc_burst_buffer_status,
	},{
		.msg_type = REQUEST_CRONTAB,
		.func = _slurm_rpc_request_crontab,
	},{
		.msg_type = REQUEST_UPDATE_CRONTAB,
		.func = _slurm_rpc_update_crontab,
	},{
		.msg_type = REQUEST_TLS_CERT,
		.func = _slurm_rpc_tls_cert,
	},{
		.msg_type = REQUEST_NODE_ALIAS_ADDRS,
		.func = _slurm_rpc_node_alias_addrs,
	},{
		.msg_type = REQUEST_DBD_RELAY,
		.func = _slurm_rpc_dbd_relay,
	},{	/* terminate the array. this must be last. */
		.msg_type = 0,
		.func = NULL,
	}
};

extern slurmctld_rpc_t *find_rpc(uint16_t msg_type)
{
	for (slurmctld_rpc_t *q = slurmctld_rpcs; q->msg_type; q++) {
		if (q->msg_type == msg_type) {
			xassert(q->func);
			return q;
		}
	}

	return NULL;
}

/* Return 1 when writeable and readable or 0 on error */
static bool _fd_is_stale(int fd)
{
	bool stale = false;
	char temp[2];
	int flags = 0;
	bool nonblocking = true;

#ifdef MSG_DONTWAIT
	flags |= MSG_DONTWAIT;

	if (!(nonblocking = fd_is_nonblocking(fd)))
		fd_set_nonblocking(fd);
#endif

	if (send(fd, NULL, 0, flags)) {
		log_flag(NET, "%s: [fd:%d] stale socket is not writable",
		       __func__, fd);
		stale = true;
	} else if (recv(fd, &temp, 1, MSG_PEEK)) {
		log_flag(NET, "%s: [fd:%d] stale socket is not readable",
		       __func__, fd);
		stale = true;
	} else {
		log_flag(NET, "%s: [fd:%d] socket is not stale", __func__, fd);
	}

	if (!nonblocking)
		fd_set_blocking(fd);

	return stale;
}

static bool _is_connection_stale(slurm_msg_t *msg, slurmctld_rpc_t *this_rpc,
				 int fd)
{
	if ((fd >= 0) && !_fd_is_stale(fd)) {
		error("%s: [fd:%d] Connection is stale, discarding RPC %s from uid:%u",
		      __func__, fd, rpc_num2string(msg->msg_type),
		      msg->auth_uid);
		return true;
	}

	if (msg->conmgr_con && !conmgr_con_is_output_open(msg->conmgr_con)) {
		error("%s: [%s] Connection is stale, discarding RPC %s from uid:%u",
		      __func__, conmgr_con_get_name(msg->conmgr_con),
		      rpc_num2string(msg->msg_type), msg->auth_uid);
		return true;
	}

	return false;
}

extern void slurmctld_req(slurm_msg_t *msg, slurmctld_rpc_t *this_rpc)
{
	DEF_TIMERS;
	int fd = -1;

	if (!msg->auth_ids_set) {
		error("%s: received message without previously validated auth",
		      __func__);
		return;
	}

	/* Debug the protocol layer.
	 */
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		START_TIMER;

	if (msg->conn) {
		fd = conn_g_get_fd(msg->conn);
		xassert(!msg->conmgr_con);
	} else if (msg->pcon && msg->pcon->conn) {
		fd = conn_g_get_fd(msg->pcon->conn);
		xassert(!msg->conmgr_con);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_PROTOCOL) {
		const char *p = rpc_num2string(msg->msg_type);

		if (msg->conmgr_con) {
			info("%s: [%s] received opcode %s uid %u",
			     __func__, conmgr_con_get_name(msg->conmgr_con), p,
			     msg->auth_uid);
		} else if (msg->pcon) {
			info("%s: received opcode %s from persist conn on (%s)%s uid %u",
			     __func__, p, msg->pcon->cluster_name,
			     msg->pcon->rem_host, msg->auth_uid);
		} else if (msg->address.ss_family != AF_UNSPEC) {
			info("%s: received opcode %s from %pA uid %u",
			     __func__, p, &msg->address, msg->auth_uid);
		} else {
			slurm_addr_t cli_addr = {
				.ss_family = AF_UNSPEC,
			};

			if ((fd >= 0) && !slurm_get_peer_addr(fd, &cli_addr))
				info("%s: received opcode %s from %pA uid %u",
				     __func__, p, &cli_addr, msg->auth_uid);
			else
				info("%s: received opcode %s from (unresolvable socket peer) uid %u",
				     __func__, p, msg->auth_uid);
		}
	}

	debug2("Processing RPC: %s from UID=%u",
	       rpc_num2string(msg->msg_type), msg->auth_uid);

	/* do not record RPC stats when stale as RPC not processed */
	if (this_rpc->skip_stale && _is_connection_stale(msg, this_rpc, fd))
		return;

	(*(this_rpc->func))(msg);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		END_TIMER;
		record_rpc_stats(msg, DELTA_TIMER);
	}
}

static void _srun_agent_launch(slurm_addr_t *addr, char *tls_cert, char *host,
			       slurm_msg_type_t type, void *msg_args,
			       uid_t r_uid, uint16_t protocol_version)
{
	agent_arg_t *agent_args = xmalloc(sizeof(agent_arg_t));

	agent_args->node_count = 1;
	agent_args->retry      = 0;
	agent_args->addr       = addr;
	agent_args->hostlist   = hostlist_create(host);
	agent_args->msg_type   = type;
	agent_args->msg_args   = msg_args;
	agent_args->tls_cert = xstrdup(tls_cert);
	set_agent_arg_r_uid(agent_args, r_uid);

	/*
	 * A federated job could have been submitted to a higher versioned
	 * origin cluster (job_ptr->start_protocol_ver), so we need to talk at
	 * the highest version that that THIS cluster understands.
	 */
	agent_args->protocol_version = MIN(SLURM_PROTOCOL_VERSION,
					   protocol_version);

	agent_queue_request(agent_args);
}

static bool _pending_het_jobs(job_record_t *job_ptr)
{
	job_record_t *het_job_leader, *het_job;
	list_itr_t *iter;
	bool pending_job = false;

	if (job_ptr->het_job_id == 0)
		return false;

	het_job_leader = find_job_record(job_ptr->het_job_id);
	if (!het_job_leader) {
		error("Hetjob leader %pJ not found", job_ptr);
		return false;
	}
	if (!het_job_leader->het_job_list) {
		error("Hetjob leader %pJ lacks het_job_list",
		      job_ptr);
		return false;
	}

	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		if (het_job_leader->het_job_id != het_job->het_job_id) {
			error("%s: Bad het_job_list for %pJ",
			      __func__, het_job_leader);
			continue;
		}
		if (IS_JOB_PENDING(het_job)) {
			pending_job = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	return pending_job;
}

static void _free_srun_alloc(void *x)
{
	resource_allocation_response_msg_t *alloc_msg;

	alloc_msg = (resource_allocation_response_msg_t *) x;
	/* NULL working_cluster_rec because it's pointing to global memory */
	alloc_msg->working_cluster_rec = NULL;
	slurm_free_resource_allocation_response_msg(alloc_msg);
}

/*
 * srun_allocate - notify srun of a resource allocation
 * IN job_ptr - job allocated resources
 */
extern void srun_allocate(job_record_t *job_ptr)
{
	job_record_t *het_job, *het_job_leader;
	resource_allocation_response_msg_t *msg_arg = NULL;
	slurm_addr_t *addr;
	list_itr_t *iter;
	list_t *job_resp_list = NULL;

	xassert(job_ptr);
	if (!job_ptr || !job_ptr->alloc_resp_port || !job_ptr->alloc_node ||
	    !job_ptr->resp_host || !job_ptr->job_resrcs ||
	    !job_ptr->job_resrcs->cpu_array_cnt)
		return;

	if (conn_tls_enabled() && !job_ptr->alloc_tls_cert)
		return;

	if (job_ptr->het_job_id == 0) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, job_ptr->alloc_resp_port,
			job_ptr->resp_host);

		msg_arg = build_alloc_msg(job_ptr, SLURM_SUCCESS, NULL);
		log_flag(TLS, "Certificate for allocation response listening socket:\n%s\n",
			 job_ptr->alloc_tls_cert);
		_srun_agent_launch(addr, job_ptr->alloc_tls_cert,
				   job_ptr->alloc_node,
				   RESPONSE_RESOURCE_ALLOCATION, msg_arg,
				   job_ptr->user_id,
				   job_ptr->start_protocol_ver);
	} else if (_pending_het_jobs(job_ptr)) {
		return;
	} else if ((het_job_leader = find_job_record(job_ptr->het_job_id))) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, het_job_leader->alloc_resp_port,
			       het_job_leader->resp_host);
		job_resp_list = list_create(_free_srun_alloc);
		iter = list_iterator_create(het_job_leader->het_job_list);
		while ((het_job = list_next(iter))) {
			if (het_job_leader->het_job_id !=
				het_job->het_job_id) {
				error("%s: Bad het_job_list for %pJ",
				      __func__, het_job_leader);
				continue;
			}
			msg_arg = build_alloc_msg(het_job, SLURM_SUCCESS,
						  NULL);
			list_append(job_resp_list, msg_arg);
			msg_arg = NULL;
		}
		list_iterator_destroy(iter);
		_srun_agent_launch(addr, job_ptr->alloc_tls_cert,
				   job_ptr->alloc_node,
				   RESPONSE_HET_JOB_ALLOCATION, job_resp_list,
				   job_ptr->user_id,
				   job_ptr->start_protocol_ver);
	} else {
		error("%s: Can not find hetjob leader %pJ",
		      __func__, job_ptr);
	}
}
