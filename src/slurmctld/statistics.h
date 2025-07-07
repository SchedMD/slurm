/*****************************************************************************\
 *  statistics.h
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _STATISTICS_H
#define _STATISTICS_H

#include "src/slurmctld/slurmctld.h"

typedef struct node_stats {
	uint16_t cpus_alloc;
	uint16_t cpus_efctv;
	uint16_t cpus_idle;
	uint16_t cpus_total;
	uint64_t mem_alloc;
	uint64_t mem_avail;
	uint64_t mem_free;
	uint64_t mem_total;
	char *name;
	uint32_t node_state;
} node_stats_t;

typedef struct nodes_statistics {
	uint16_t alloc;
	uint16_t cg;
	uint16_t down;
	uint16_t drain;
	uint16_t draining;
	uint16_t fail;
	uint16_t future;
	uint16_t idle;
	uint16_t invalid_reg;
	uint16_t maint;
	uint16_t mixed;
	uint16_t no_resp;
	uint32_t node_stats_count;
	node_stats_t **node_stats_table;
	uint16_t planned;
	uint16_t reboot_requested;
	uint16_t resv;
	uint16_t unknown;
} nodes_stats_t;

typedef struct partition_statistics {
	uint32_t jobs; /* number of unfinished jobs in the partition. */
	uint32_t jobs_bootfail;
	uint32_t jobs_cancelled;
	uint32_t jobs_completed;
	uint32_t jobs_completing;
	uint32_t jobs_configuring;
	uint16_t jobs_cpus_alloc;
	uint32_t jobs_deadline;
	uint32_t jobs_failed;
	uint32_t jobs_hold;
	uint16_t jobs_max_job_nodes; /* max of max num of nodes requested among
				      * all pending jobs in the partition. */
	uint16_t jobs_max_job_nodes_nohold; /* excludes held jobs */
	uint64_t jobs_memory_alloc;
	uint16_t jobs_min_job_nodes; /* max of min num of nodes requested among
				      * all pending jobs in the partition. */
	uint16_t jobs_min_job_nodes_nohold; /* excludes held jobs */
	uint32_t jobs_node_failed;
	uint32_t jobs_oom;
	uint32_t jobs_pending;
	uint32_t jobs_powerup_node;
	uint32_t jobs_preempted;
	uint32_t jobs_requeued;
	uint32_t jobs_running;
	uint32_t jobs_stageout;
	uint32_t jobs_suspended;
	uint32_t jobs_timeout;
	uint32_t jobs_wait_part_node_limit;
	char *name; /* name of the partition */
	uint16_t nodes_alloc;
	uint16_t nodes_cg;
	uint16_t nodes_cpus_alloc;
	uint16_t nodes_cpus_efctv;
	uint16_t nodes_cpus_idle;
	uint16_t nodes_down;
	uint16_t nodes_drain;
	uint16_t nodes_draining;
	uint16_t nodes_fail;
	uint16_t nodes_future;
	uint16_t nodes_idle;
	uint16_t nodes_maint;
	uint64_t nodes_mem_alloc;
	uint64_t nodes_mem_avail;
	uint64_t nodes_mem_free;
	uint64_t nodes_mem_total;
	uint16_t nodes_mixed;
	uint16_t nodes_no_resp;
	uint16_t nodes_planned;
	uint16_t nodes_reboot_requested;
	uint16_t nodes_resv;
	uint16_t nodes_unknown;
	uint32_t total_cpus; /* number of CPUs associated with the partition. */
	uint16_t total_nodes; /* number of total nodes in the partition */
} partition_stats_t;

typedef struct partitions_statistics {
	list_t *parts;
} partitions_stats_t;

typedef struct scheduling_statistics {
	uint32_t agent_count;
	uint32_t agent_queue_size;
	uint32_t agent_thread_count;
	uint32_t bf_depth_mean;
	uint32_t bf_mean_cycle;
	uint32_t bf_mean_table_sz;
	uint32_t bf_queue_len_mean;
	uint32_t bf_try_depth_mean;
	diag_stats_t *diag_stats;
	uint64_t last_proc_req_start;
	uint32_t sched_mean_cycle;
	uint32_t sched_mean_depth_cycle;
	uint32_t server_thread_count;
	uint32_t slurmdbd_queue_size;
	time_t time;
} scheduling_stats_t;

typedef struct job_statistics {
	char *account;
	uint16_t cpus_alloc;
	uint32_t job_array_cnt; /* If job array and PD, number of array tasks */
	uint32_t job_id;
	uint32_t job_state;
	uint16_t max_nodes;
	uint64_t memory_alloc;
	uint16_t min_nodes;
	uint16_t nodes_alloc;
	char *partition;
	uint32_t state_reason;
	char *user_name;
} job_stats_t;

typedef struct jobs_statistics {
	uint32_t bootfail;
	uint32_t cancelled;
	uint32_t completed;
	uint32_t completing;
	uint32_t configuring;
	uint16_t cpus_alloc;
	uint32_t deadline;
	uint32_t failed;
	uint32_t hold;
	uint32_t job_cnt;
	list_t *jobs;
	uint64_t memory_alloc;
	uint16_t nodes_alloc;
	uint32_t node_failed;
	uint32_t oom;
	uint32_t pending;
	uint32_t powerup_node;
	uint32_t preempted;
	uint32_t requeued;
	uint32_t running;
	uint32_t stageout;
	uint32_t suspended;
	uint32_t timeout;
} jobs_stats_t;

typedef struct ua_statistics {
	char *name; /* name of the user or account name */
	jobs_stats_t *s; /* aggregated statistics for this user or account */
} ua_stats_t;

typedef struct users_accts_statistics {
	list_t *accounts;
	list_t *users;
} users_accts_stats_t;

/* Pack all scheduling statistics */
extern buf_t *pack_all_stat(uint16_t protocol_version);

/* Reset all scheduling statistics
 * level IN - clear backfilled_jobs count if set */
extern void reset_stats(int level);

/*
 * Get a struct with all jobs statistics
 * IN lock - whether to lock the controller or not to get jobs
 * RET - pointer to an allocated struct with consolidated statistics or NULL
 */
extern jobs_stats_t *statistics_get_jobs(bool lock);

/*
 * Get a struct with all nodes statistics
 * IN lock - whether to lock the controller or not to get nodes
 * RET - pointer to an allocated struct with consolidated statistics or NULL
 */
extern nodes_stats_t *statistics_get_nodes(bool lock);

/*
 * Get a struct with all partition statistics
 * IN ns - struct with nodes statistics to aggregate to the partitions stats
 * IN js - struct with jobs statistics to aggregate to the partitions stats
 * IN lock - whether to lock the controller or not to get partitions
 * RET - pointer to an allocated struct with consolidated statistics or NULL
 */
extern partitions_stats_t *statistics_get_parts(nodes_stats_t *ns,
						jobs_stats_t *js, bool lock);

/*
 * Get a struct with all scheduling statistics, same as sdiag
 * RET - pointer to an allocated struct with consolidated statistics or NULL
 */
extern scheduling_stats_t *statistics_get_sched(void);

/*
 * Get a struct with all nodes statistics
 * IN lock - whether to lock the controller or not to get nodes
 * RET - pointer to a struct with consolidated statistics or NULL
 */
extern users_accts_stats_t *statistics_get_users_accounts(jobs_stats_t *js);

extern void statistics_free_jobs(jobs_stats_t *s);
extern void statistics_free_nodes(nodes_stats_t *s);
extern void statistics_free_parts(partitions_stats_t *s);
extern void statistics_free_sched(scheduling_stats_t *s);
extern void statistics_free_users_accounts(users_accts_stats_t *s);
#endif
