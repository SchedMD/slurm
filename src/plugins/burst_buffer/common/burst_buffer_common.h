/*****************************************************************************\
 *  burst_buffer_common.h - Common header for managing burst_buffers
 *
 *  NOTE: These functions are designed so they can be used by multiple burst
 *  buffer plugins at the same time (e.g. you might provide users access to
 *  both burst_buffer/cray and burst_buffer/generic on the same system), so
 *  the state information is largely in the individual plugin and passed as
 *  a pointer argument to these functions.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef __BURST_BUFFER_COMMON_H__
#define __BURST_BUFFER_COMMON_H__

#include "src/common/pack.h"
#include "slurm/slurm.h"

/* Interval, in seconds, for purging orphan bb_alloc_t records and timing out
 * staging */
#define AGENT_INTERVAL	10

/* Hash tables are used for both job burst buffer and user limit records */
#define BB_HASH_SIZE	100

/* Burst buffer configuration parameters */
typedef struct bb_config {
	uid_t   *allow_users;
	char    *allow_users_str;
	bool	debug_flag;
	uid_t   *deny_users;
	char    *deny_users_str;
	char    *get_sys_state;
	uint64_t granularity;		/* space allocation granularity,
					 * units are GB */
	uint32_t gres_cnt;		/* Count of records in gres_ptr */
	burst_buffer_gres_t *gres_ptr;	/* Type is defined in slurm.h */
	uint64_t job_size_limit;
	uint32_t prio_boost_alloc;
	uint32_t prio_boost_use;
	uint16_t private_data;
	uint32_t stage_in_timeout;
	uint32_t stage_out_timeout;
	char    *start_stage_in;
	char    *start_stage_out;
	char    *stop_stage_in;
	char    *stop_stage_out;
	uint64_t user_size_limit;
} bb_config_t;

typedef struct bb_alloc {
	uint32_t array_job_id;
	uint32_t array_task_id;
	bool cancelled;
	time_t end_time;	/* Expected time when use will end */
	uint32_t gres_cnt;	/* Count of records in gres_ptr */
	burst_buffer_gres_t *gres_ptr;
	uint32_t job_id;
	char *name;		/* For persistent burst buffers */
	struct bb_alloc *next;
	time_t seen_time;	/* Time buffer last seen */
	uint64_t size;
	uint16_t state;
	time_t state_time;	/* Time of last state change */
	time_t use_time;	/* Expected time when use will begin */
	uint32_t user_id;
} bb_alloc_t;

typedef struct bb_user {
	struct bb_user *next;
	uint64_t size;
	uint32_t user_id;
} bb_user_t;

typedef struct {
	char *   name;		/* Generic burst buffer resource, e.g. "nodes" */
	uint64_t count;		/* Count of required resources */
} bb_gres_t;

typedef struct {
	uint32_t   gres_cnt;	/* number of records in gres_ptr */
	bb_gres_t *gres_ptr;
	uint32_t   swap_size;	/* swap space required per node */
	uint32_t   swap_nodes;	/* Number of nodes needed */
	uint64_t   total_size;	/* Total GB required for this job */
} bb_job_t;

typedef struct job_queue_rec {
	uint64_t bb_size;	/* Used by generic plugin only */
	bb_job_t *bb_spec;	/* Used by cray plugin only */
	struct job_record *job_ptr;
} job_queue_rec_t;

struct preempt_bb_recs {
	bb_alloc_t *bb_ptr;
	uint32_t job_id;
	uint64_t size;
	time_t   use_time;
	uint32_t user_id;
};

typedef struct bb_state {
	bb_config_t	bb_config;
	bb_alloc_t **	bb_hash;	/* Hash by job_id */
	bb_user_t **	bb_uhash;	/* Hash by user_id */
	pthread_mutex_t	bb_mutex;
	pthread_t	bb_thread;
	time_t		last_load_time;
	char *		name;		/* Plugin name */
	time_t		next_end_time;
	pthread_cond_t	term_cond;
	bool		term_flag;
	pthread_mutex_t	term_mutex;
	uint64_t	total_space;	/* units are GB */
	uint64_t	used_space;	/* units are GB */
} bb_state_t;

/* Add a burst buffer allocation to a user's load */
extern void bb_add_user_load(bb_alloc_t *bb_ptr, bb_state_t *state_ptr);

/* Allocate burst buffer hash tables */
extern void bb_alloc_cache(bb_state_t *state_ptr);

/* Allocate a per-job burst buffer record for a specific job.
 * Return a pointer to that record.
 * Use bb_free_rec() to purge the returned record. */
extern bb_alloc_t *bb_alloc_job_rec(bb_state_t *state_ptr,
				    struct job_record *job_ptr,
				    bb_job_t *bb_spec);

/* Allocate a burst buffer record for a job and increase the job priority
 * if so configured.
 * Use bb_free_rec() to purge the returned record. */
extern bb_alloc_t *bb_alloc_job(bb_state_t *state_ptr,
				struct job_record *job_ptr, bb_job_t *bb_spec);

/* Allocate a named burst buffer record for a specific user.
 * Return a pointer to that record.
 * Use bb_free_rec() to purge the returned record. */
extern bb_alloc_t *bb_alloc_name_rec(bb_state_t *state_ptr, char *name,
				     uint32_t user_id);

/* Clear all cached burst buffer records, freeing all memory. */
extern void bb_clear_cache(bb_state_t *state_ptr);

/* Clear configuration parameters, free memory
 * config_ptr IN - Initial configuration to be cleared
 * fini IN - True if shutting down, do more complete clean-up */
extern void bb_clear_config(bb_config_t *config_ptr, bool fini);

/* Find a per-job burst buffer record for a specific job.
 * If not found, return NULL. */
extern bb_alloc_t *bb_find_job_rec(struct job_record *job_ptr,
				   bb_alloc_t **bb_hash);

/* Find a per-user burst buffer record for a specific user ID */
extern bb_user_t *bb_find_user_rec(uint32_t user_id, bb_user_t **bb_uhash);

/* Free memory associated with allocated bb record */
extern void bb_free_rec(bb_alloc_t *bb_ptr);

/* Translate a burst buffer size specification in string form to numeric form,
 * recognizing various sufficies (MB, GB, TB, PB, and Nodes). */
extern uint64_t bb_get_size_num(char *tok, uint64_t granularity);

/* Round up a number based upon some granularity */
extern uint64_t bb_granularity(uint64_t start_size, uint64_t granularity);

extern void bb_job_queue_del(void *x);

/* Sort job queue by expected start time */
extern int bb_job_queue_sort(void *x, void *y);

/* Load and process configuration parameters */
extern void bb_load_config(bb_state_t *state_ptr, char *plugin_type);

/* Pack individual burst buffer records into a  buffer */
extern int bb_pack_bufs(uid_t uid, bb_alloc_t **bb_hash, Buf buffer,
			uint16_t protocol_version);

/* Pack state and configuration parameters into a buffer */
extern void bb_pack_state(bb_state_t *state_ptr, Buf buffer,
			  uint16_t protocol_version);

/* Sort preempt_bb_recs in order of DECREASING use_time */
extern int bb_preempt_queue_sort(void *x, void *y);

/* Remove a burst buffer allocation from a user's load */
extern void bb_remove_user_load(bb_alloc_t *bb_ptr, bb_state_t *state_ptr);

/* For each burst buffer record, set the use_time to the time at which its
 * use is expected to begin (i.e. each job's expected start time) */
extern void bb_set_use_time(bb_state_t *state_ptr);

/* Sleep function, also handles termination signal */
extern void bb_sleep(bb_state_t *state_ptr, int add_secs);

/* Execute a script, wait for termination and return its stdout.
 * script_type IN - Type of program being run (e.g. "StartStageIn")
 * script_path IN - Fully qualified pathname of the program to execute
 * script_args IN - Arguments to the script
 * max_wait IN - Maximum time to wait in milliseconds,
 *		 -1 for no limit (asynchronous)
 * status OUT - Job exit code
 * Return stdout+stderr of spawned program, value must be xfreed. */
extern char *bb_run_script(char *script_type, char *script_path,
			   char **script_argv, int max_wait, int *status);

#endif	/* __BURST_BUFFER_COMMON_H__ */
