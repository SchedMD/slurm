#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backfill.h"
#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/hostlist.h"
#include "src/common/job_features.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

uint32_t slurm_daemon = IS_SLURMCTLD;

bool avail_front_end(job_record_t *job_ptr)
{
	return true;
}

void job_set_alloc_tres(job_record_t *job_ptr, bool assoc_mgr_locked)
{
	uint32_t alloc_nodes = 0;

	xfree(job_ptr->tres_alloc_str);
	xfree(job_ptr->tres_alloc_cnt);
	xfree(job_ptr->tres_fmt_alloc_str);

	job_ptr->tres_alloc_cnt = xcalloc(slurmctld_tres_cnt, sizeof(uint64_t));

	job_ptr->tres_alloc_cnt[TRES_ARRAY_CPU] =
		(uint64_t) job_ptr->total_cpus;

	alloc_nodes = job_ptr->node_cnt;
	job_ptr->tres_alloc_cnt[TRES_ARRAY_NODE] = (uint64_t) alloc_nodes;
	job_ptr->tres_alloc_cnt[TRES_ARRAY_MEM] =
		job_ptr->details->pn_min_memory;

	job_ptr->tres_alloc_cnt[TRES_ARRAY_ENERGY] = NO_VAL64;
}

int job_test_resv(job_record_t *job_ptr, time_t *when, bool move_time,
		  bitstr_t **node_bitmap, resv_exc_t *resv_exc_ptr,
		  bool *resv_overlap, bool reboot)
{
	debug("%s %pJ", __func__, job_ptr);
	*node_bitmap = node_conf_get_active_bitmap();
	return SLURM_SUCCESS;
}

int job_test_lic_resv(job_record_t *job_ptr, licenses_id_t id, time_t when,
		      bool reboot)
{
	debug("%s %pJ", __func__, job_ptr);
	return SLURM_SUCCESS;
}

void resv_replace_update(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
}

void job_time_adj_resv(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
}

bool job_independent(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
	return true;
}

uint64_t job_get_tres_mem(struct job_resources *job_res, uint64_t pn_min_memory,
			  uint32_t cpu_cnt, uint32_t node_cnt,
			  part_record_t *part_ptr, list_t *gres_list,
			  bool user_set_mem, uint16_t min_sockets_per_node,
			  uint32_t num_tasks)
{
	uint64_t mem_total = 0;

	return mem_total;
}

uint16_t job_get_sockets_per_node(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
	return 1;
}

void job_state_set(job_record_t *job_ptr, uint32_t state)
{
	debug("%s %pJ %u", __func__, job_ptr, state);
	job_ptr->job_state = state;
}

void job_state_unset_flag(job_record_t *job_ptr, uint32_t flag)
{
	uint32_t job_state;

	job_state = job_ptr->job_state & ~flag;
	job_ptr->job_state = job_state;
}

void launch_job(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
}

void gres_stepmgr_set_job_tres_cnt(list_t *gres_list, uint32_t node_cnt,
				   uint64_t *tres_cnt, bool locked)
{
	;
}

extern void gres_stepmgr_set_node_tres_cnt(list_t *gres_list,
					   uint64_t *tres_cnt, bool locked)
{
	;
}

void gres_stepmgr_job_clear_alloc(list_t *job_gres_list)
{
	;
}

int gres_stepmgr_job_alloc(list_t *job_gres_list, list_t **job_gres_list_alloc,
			   list_t *node_gres_list, int node_cnt, int node_index,
			   int node_offset, uint32_t job_id, char *node_name,
			   bitstr_t *core_bitmap, bool new_alloc)
{
	debug("%s job_id:%u", __func__, job_id);
	return SLURM_SUCCESS;
}

int gres_stepmgr_job_alloc_whole_node(list_t *job_gres_list,
				      list_t **job_gres_list_alloc,
				      list_t *node_gres_list, int node_cnt,
				      int node_index, int node_offset,
				      uint32_t job_id, char *node_name,
				      bitstr_t *core_bitmap, bool new_alloc)
{
	debug("%s job_id:%u", __func__, job_id);
	return SLURM_SUCCESS;
}

void gres_stepmgr_job_build_details(list_t *job_gres_list, char *nodes,
				    uint32_t *gres_detail_cnt,
				    char ***gres_detail_str,
				    char **total_gres_str)
{
	;
}

int gres_stepmgr_job_dealloc(list_t *job_gres_list, list_t *node_gres_list,
			     int node_offset, uint32_t job_id, char *node_name,
			     bool old_job, bool resize)
{
	debug("%s job_id:%u", __func__, job_id);
	return SLURM_SUCCESS;
}

static int _list_find_job_id(void *job_entry, void *key)
{
	job_record_t *job_ptr = (job_record_t *) job_entry;
	uint32_t *job_id_ptr = (uint32_t *) key;
	if (job_ptr->job_id == *job_id_ptr)
		return 1;

	return 0;
}

job_record_t *find_job_record(uint32_t job_id)
{
	return list_find_first(job_list, _list_find_job_id, &job_id);
}

int fed_mgr_job_unlock(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
	return SLURM_SUCCESS;
}

int fed_mgr_job_lock(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
	return SLURM_SUCCESS;
}

int fed_mgr_job_start(job_record_t *job_ptr, time_t start_time)
{
	debug("%s %pJ", __func__, job_ptr);
	return SLURM_SUCCESS;
}

int fed_mgr_job_end(job_record_t *job_ptr, time_t start_time)
{
	debug("%s %pJ", __func__, job_ptr);
	return SLURM_SUCCESS;
}

bool fed_mgr_sibs_synced(void)
{
	return true;
}

void srun_allocate(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
}

void acct_policy_add_job_submit(job_record_t *job_ptr, bool assoc_locked)
{
	debug("%s %pJ", __func__, job_ptr);
}

void acct_policy_job_fini(job_record_t *job_ptr, bool assoc_locked)
{
	debug("%s %pJ", __func__, job_ptr);
}

void acct_policy_job_begin(job_record_t *job_ptr, bool assoc_locked)
{
	debug("%s %pJ", __func__, job_ptr);
}

bool acct_policy_job_runnable_post_select(job_record_t *job_ptr,
					  uint64_t *tres_req_cnt,
					  bool assoc_mgr_locked)
{
	debug("%s %pJ", __func__, job_ptr);
	return true;
}

bool acct_policy_job_runnable_pre_select(job_record_t *job_ptr,
					 bool assoc_mgr_locked)
{
	debug("%s %pJ", __func__, job_ptr);
	return true;
}

int acct_policy_handle_accrue_time(job_record_t *job_ptr, bool assoc_mgr_locked)
{
	debug("%s %pJ", __func__, job_ptr);
	return SLURM_SUCCESS;
}

void acct_policy_alter_job(job_record_t *job_ptr, uint32_t new_time_limit)
{
	debug("%s %pJ", __func__, job_ptr);
}

uint32_t acct_policy_get_prio_thresh(job_record_t *job_ptr,
				     bool assoc_mgr_locked)
{
	debug("%s %pJ", __func__, job_ptr);
	return 0;
}

uint32_t acct_policy_get_max_nodes(job_record_t *job_ptr, uint32_t *wait_reason)
{
	debug("%s %pJ", __func__, job_ptr);
	return (uint32_t) (INFINITE64);
}

void job_completion_logger(job_record_t *job_ptr, bool requeue)
{
	debug("%s %pJ", __func__, job_ptr);
}

extern void build_cg_bitmap(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
}

void reservation_delete_resv_exc_parts(resv_exc_t *resv_exc)
{
	debug("%s", __func__);
}

list_t *slurm_find_preemptable_jobs(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
	return NULL;
}

uint16_t slurm_job_preempt_mode(job_record_t *job_ptr)
{
	return PREEMPT_MODE_OFF;
}

bool slurm_preemption_enabled(void)
{
	return false;
}

int bb_g_load_state(bool init_config)
{
	return SLURM_SUCCESS;
}

int bb_g_job_test_stage_in(job_record_t *job_ptr, bool test_only)
{
	return 1;
}

int bb_g_job_begin(job_record_t *job_ptr)
{
	return SLURM_SUCCESS;
}

int validate_group(part_record_t *part_ptr, uid_t run_uid)
{
	return 1;
}

void set_job_failed_assoc_qos_ptr(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
	return;
}

int part_policy_valid_qos(part_record_t *part_ptr, slurmdb_qos_rec_t *qos_ptr,
			  uid_t submit_uid, job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
	return SLURM_SUCCESS;
}

int part_policy_valid_acct(part_record_t *part_ptr, char *acct,
			   job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
	return SLURM_SUCCESS;
}

int list_find_part(void *x, void *key)
{
	part_record_t *part_ptr = (part_record_t *) x;
	char *part = (char *) key;

	return (!xstrcmp(part_ptr->name, part));
}

part_record_t *find_part_record(char *name)
{
	if (!part_list) {
		error("part_list is NULL");
		return NULL;
	}
	return list_find_first(part_list, &list_find_part, name);
}

int job_limits_check(job_record_t **job_pptr, bool check_min_time)
{
	debug("%s %pJ", __func__, *job_pptr);
	return WAIT_NO_REASON;
}

void job_end_time_reset(job_record_t *job_ptr)
{
	if (job_ptr->time_limit == INFINITE) {
		job_ptr->end_time = job_ptr->start_time +
				    (365 * 24 * 60 * 60); /* secs in year */
	} else {
		job_ptr->end_time = job_ptr->start_time +
				    (job_ptr->time_limit * 60); /* secs */
	}
	job_ptr->end_time_exp = job_ptr->end_time;
}

void job_resv_append_magnetic(job_queue_req_t *job_queue_req)
{
	;
}

void job_resv_clear_magnetic_flag(job_record_t *job_ptr)
{
	;
}

void job_array_pre_sched(job_record_t *job_ptr)
{
	return;
}

void job_array_start(job_record_t *job_ptr)
{
	return;
}

job_record_t *job_array_post_sched(job_record_t *job_ptr, bool list_add)
{
	debug("%s %pJ", __func__, job_ptr);
	return job_ptr;
}

uint16_t job_mgr_determine_cpus_per_core(job_details_t *details, int node_inx)
{
	return 1;
}

void prolog_slurmctld(job_record_t *job_ptr)
{
	debug("%s %pJ", __func__, job_ptr);
}

bool power_save_test(void)
{
	return false;
}

void make_node_alloc(node_record_t *node_ptr, job_record_t *job_ptr)
{
	uint32_t node_flags;

	(node_ptr->run_job_cnt)++;
	bit_clear(idle_node_bitmap, node_ptr->index);
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
}

void node_mgr_make_node_blocked(job_record_t *job_ptr, bool set)
{
	return;
}

void gs_job_start(job_record_t *job_ptr)
{
	return;
}

void lock_slurmctld(slurmctld_lock_t lock_levels)
{
	return;
}

void unlock_slurmctld(slurmctld_lock_t lock_levels)
{
	return;
}
