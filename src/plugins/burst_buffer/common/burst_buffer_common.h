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

#ifndef __BURST_BUFFER_COMMON_H__
#define __BURST_BUFFER_COMMON_H__

#include "src/common/list.h"
#include "src/common/pack.h"
#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

/* Interval, in seconds, for purging orphan bb_alloc_t records and timing out
 * staging */
#define AGENT_INTERVAL	30

/* Hash tables are used for both job burst buffer and user limit records */
#define BB_HASH_SIZE	100

/* Default operation timeouts */
#define DEFAULT_OTHER_TIMEOUT		300	/* 5 minutes */
#define DEFAULT_STATE_IN_TIMEOUT	86400	/* 1 day */
#define DEFAULT_STATE_OUT_TIMEOUT	86400	/* 1 day */
#define DEFAULT_VALIDATE_TIMEOUT	5	/* 5 seconds */

/* Burst buffer configuration parameters */
typedef struct bb_config {
	uid_t   *allow_users;
	char    *allow_users_str;
	char    *create_buffer;
	bool	debug_flag;
	char	*default_pool;
	uid_t   *deny_users;
	char    *deny_users_str;
	char    *destroy_buffer;
	uint32_t flags;			/* See BB_FLAG_* in slurm.h */
	char    *get_sys_state;
	char    *get_sys_status;
	uint64_t granularity;		/* space allocation granularity,
					 * units are GB */
	uint32_t pool_cnt;		/* Count of records in pool_ptr */
	burst_buffer_pool_t *pool_ptr;	/* Type is defined in slurm.h */
	uint32_t other_timeout;
	uint32_t stage_in_timeout;
	uint32_t stage_out_timeout;
	char    *start_stage_in;
	char    *start_stage_out;
	char    *stop_stage_in;
	char    *stop_stage_out;
	uint32_t validate_timeout;
} bb_config_t;

/* Current burst buffer allocations (instances). Some of these will be job
 * specific (job_id != 0) and others persistent */
#define BB_ALLOC_MAGIC		0xDEAD3448
typedef struct bb_alloc {
	char *account;		/* Associated account (for limits) */
	slurmdb_assoc_rec_t *assoc_ptr;
	char *assocs;		/* Association string, used for accounting */
	uint32_t array_job_id;
	uint32_t array_task_id;
	bool cancelled;
	time_t create_time;	/* Time of creation */
	time_t end_time;	/* Expected time when use will end */
	uint32_t id;		/* ID for reservation/accounting */
	uint32_t job_id;
	uint32_t magic;
	char *name;		/* For persistent burst buffers */
	struct bb_alloc *next;
	bool orphaned;		/* Job is purged, could not stage-out data */
	char *partition;	/* Associated partition (for limits) */
	char *pool;		/* Resource (pool) used */
	char *qos;		/* Associated QOS (for limits) */
	slurmdb_qos_rec_t *qos_ptr;
	time_t seen_time;	/* Time buffer last seen */
	uint64_t size;
	uint16_t state;
	time_t state_time;	/* Time of last state change */
	time_t use_time;	/* Expected time when use will begin */
	uint32_t user_id;
} bb_alloc_t;

/* User's storage use, needed to enforce per-user limits without TRES */
#define BB_USER_MAGIC		0xDEAD3493
typedef struct bb_user {
	uint32_t magic;
	struct bb_user *next;
	uint64_t size;
	uint32_t user_id;
} bb_user_t;

#define BB_FLAG_BB_OP		1	/* Requested using #BB prefix */
#define BB_FLAG_DW_OP		2	/* Requested using #DW prefix */

/* Burst buffer creation records with state */
typedef struct {
	char    *access;	/* Buffer access */
	uint32_t flags;		/* See BB_FLAG_* above */
	bool     create;	/* Set if buffer create requested */
	bool     destroy;	/* Set if buffer destroy requested */
	bool     hurry;		/* Fast buffer destroy */
	char    *name;		/* Buffer name, non-numeric for persistent */
	char    *pool;		/* Pool in which to create buffer */
	uint64_t size;		/* Buffer size in bytes */
	uint16_t state;		/* Buffer state, see BB_STATE_* in slurm.h.in */
	char    *type;		/* Buffer type */
	bool     use;		/* Set if persistent buffer use requested */
} bb_buf_t;

/* Burst buffer resources required for a job, based upon a job record's
 * burst_buffer string field */
#define BB_JOB_MAGIC		0xDEAD3412
typedef struct bb_job {
	char      *account;	/* Associated account (for limits) */
	uint32_t   buf_cnt;	/* Number of records in buf_ptr */
	bb_buf_t  *buf_ptr;	/* Buffer creation records */
	uint32_t   job_id;
	char      *job_pool;	/* Pool in which to create job buffers */
	uint32_t   magic;
	struct bb_job *next;
	char      *partition;	/* Associated partition (for limits) */
	uint64_t   persist_add;	/* Persistent buffer space job adds, bytes */
	char      *qos;	 	/* Associated QOS (for limits) */
	int        retry_cnt;	/* Count of attempted retries */
	uint64_t   req_size;	/* Bytes requested by job (excludes
				 * persistent buffers) */
	int        state;	/* job state with respect to burst buffers,
				 * See BB_STATE_* in slurm.h.in */
	uint32_t   swap_size;	/* swap space required per node in GB */
	uint32_t   swap_nodes;	/* Number of nodes needed */
	uint64_t   total_size;	/* Total bytes required for job (excludes
				 * persistent buffers, rounded up from
				 * req_size) */
	bool       use_job_buf;	/* True if uses job buffer,
				 * false if uses persistent buffer only */
	uint32_t   user_id;	/* user the job runs as */
} bb_job_t;

/* Used for building queue of jobs records for various purposes */
typedef struct bb_job_queue_rec {
	uint64_t bb_size;	/* Used by generic plugin only */
	bb_job_t *bb_job;	/* Used by cray plugin only */
	struct job_record *job_ptr;
} bb_job_queue_rec_t;

/* Used for building queue of job preemption candidates */
struct preempt_bb_recs {
	bb_alloc_t *bb_ptr;
	uint32_t job_id;
	char *pool;
	uint64_t size;
	time_t   use_time;
	uint32_t user_id;
};

/* Current plugin state information */
typedef struct bb_state {
	bb_config_t	bb_config;
	bb_alloc_t **	bb_ahash;	/* Allocation buffers, hash by job_id */
	bb_job_t **	bb_jhash;	/* Job state, hash by job_id */
	bb_user_t **	bb_uhash;	/* User limit, hash by user_id */
	pthread_mutex_t	bb_mutex;
	pthread_t	bb_thread;
	time_t		last_load_time;
	char *		name;		/* Plugin name */
	time_t		next_end_time;
	time_t		last_update_time;
	uint64_t	persist_resv_sz; /* Space reserved for persistent buffers */
	List		persist_resv_rec;/* List of bb_pend_persist_t records */
	pthread_cond_t	term_cond;
	bool		term_flag;
	pthread_mutex_t	term_mutex;
	uint64_t	total_space;	/* units are bytes */
	int		tres_id;	/* TRES ID, for limits */
	int		tres_pos;	/* TRES index, for limits */
	uint64_t	used_space;	/* Allocated space, in bytes */
	uint64_t	unfree_space;	/* Includes alloc_space (above) plus
					 * drained, units are bytes */
} bb_state_t;

/* Allocate burst buffer hash tables */
extern void bb_alloc_cache(bb_state_t *state_ptr);

/* Allocate a per-job burst buffer record for a specific job.
 * Return a pointer to that record.
 * Use bb_free_alloc_buf() to purge the returned record. */
extern bb_alloc_t *bb_alloc_job_rec(bb_state_t *state_ptr,
				    struct job_record *job_ptr,
				    bb_job_t *bb_job);

/* Allocate a burst buffer record for a job and increase the job priority
 * if so configured.
 * Use bb_free_alloc_buf() to purge the returned record. */
extern bb_alloc_t *bb_alloc_job(bb_state_t *state_ptr,
				struct job_record *job_ptr, bb_job_t *bb_job);

/* Allocate a named burst buffer record for a specific user.
 * Return a pointer to that record.
 * Use bb_free_alloc_buf() to purge the returned record. */
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
extern bb_alloc_t *bb_find_alloc_rec(bb_state_t *state_ptr,
				     struct job_record *job_ptr);

/* Find a burst buffer record by name
 * bb_name IN - Buffer's name
 * user_id IN - Possible user ID, advisory use only
 * RET the buffer or NULL if not found */
extern bb_alloc_t *bb_find_name_rec(char *bb_name, uint32_t user_id,
				    bb_state_t *state_ptr);

/* Find a per-user burst buffer record for a specific user ID */
extern bb_user_t *bb_find_user_rec(uint32_t user_id, bb_state_t *state_ptr);

/* Remove a specific bb_alloc_t from global records.
 * RET true if found, false otherwise */
extern bool bb_free_alloc_rec(bb_state_t *state_ptr, bb_alloc_t *bb_ptr);

/* Free memory associated with allocated bb record, caller is responsible for
 * maintaining linked list */
extern void bb_free_alloc_buf(bb_alloc_t *bb_alloc);

/* Translate a burst buffer size specification in string form to numeric form,
 * recognizing various sufficies (MB, GB, TB, PB, and Nodes). Default units
 * are bytes. */
extern uint64_t bb_get_size_num(char *tok, uint64_t granularity);

/* Translate a burst buffer size specification in numeric form to string form,
 * recognizing various sufficies (KB, MB, GB, TB, PB, and Nodes). */
extern char *bb_get_size_str(uint64_t size);

/* Round up a number based upon some granularity */
extern uint64_t bb_granularity(uint64_t start_size, uint64_t granularity);

/* Allocate a bb_job_t record, hashed by job_id, delete with bb_job_del() */
extern bb_job_t *bb_job_alloc(bb_state_t *state_ptr, uint32_t job_id);

/* Delete a bb_job_t record, hashed by job_id */
extern void bb_job_del(bb_state_t *state_ptr, uint32_t job_id);

/* Return a pointer to the existing bb_job_t record for a given job_id or
 * NULL if not found */
extern bb_job_t *bb_job_find(bb_state_t *state_ptr, uint32_t job_id);

/* Log the contents of a bb_job_t record using "info()" */
extern void bb_job_log(bb_state_t *state_ptr, bb_job_t *bb_job);

extern void bb_job_queue_del(void *x);

/* Sort job queue by expected start time */
extern int bb_job_queue_sort(void *x, void *y);

/* Load and process configuration parameters */
extern void bb_load_config(bb_state_t *state_ptr, char *plugin_type);

/* Pack individual burst buffer records into a buffer */
extern int bb_pack_bufs(uid_t uid, bb_state_t *state_ptr, Buf buffer,
			uint16_t protocol_version);

/* Pack state and configuration parameters into a buffer */
extern void bb_pack_state(bb_state_t *state_ptr, Buf buffer,
			  uint16_t protocol_version);

/* Pack individual burst buffer usage records into a buffer (used for limits) */
extern int bb_pack_usage(uid_t uid, bb_state_t *state_ptr, Buf buffer,
			 uint16_t protocol_version);

/* Sort preempt_bb_recs in order of DECREASING use_time */
extern int bb_preempt_queue_sort(void *x, void *y);

/* Set the bb_state's tres_pos for limit enforcement.
 * Value is set to -1 if not found. */
extern void bb_set_tres_pos(bb_state_t *state_ptr);

/* For each burst buffer record, set the use_time to the time at which its
 * use is expected to begin (i.e. each job's expected start time) */
extern void bb_set_use_time(bb_state_t *state_ptr);

/* Sleep function, also handles termination signal */
extern void bb_sleep(bb_state_t *state_ptr, int add_secs);

/* Make claim against resource limit for a user
 * user_id IN - Owner of burst buffer
 * bb_size IN - Size of burst buffer
 * pool IN - Pool containing the burst buffer
 * state_ptr IN - Global state to update
 * update_pool_unfree IN - If true, update the pool's unfree space */
extern void bb_limit_add(uint32_t user_id, uint64_t bb_size, char *pool,
			 bb_state_t *state_ptr, bool update_pool_unfree);

/* Release claim against resource limit for a user */
extern void bb_limit_rem(uint32_t user_id, uint64_t bb_size, char *pool,
			 bb_state_t *state_ptr);

/* Log creation of a persistent burst buffer in the database
 * job_ptr IN - Point to job that created, could be NULL at startup
 * bb_alloc IN - Pointer to persistent burst buffer state info
 * state_ptr IN - Pointer to burst_buffer plugin state info
 */
extern int bb_post_persist_create(struct job_record *job_ptr,
				  bb_alloc_t *bb_alloc, bb_state_t *state_ptr);

/* Log deletion of a persistent burst buffer in the database */
extern int bb_post_persist_delete(bb_alloc_t *bb_alloc, bb_state_t *state_ptr);

/* Determine if the specified pool name is valid on this system */
extern bool bb_valid_pool_test(bb_state_t *state_ptr, char *pool_name);

#endif	/* __BURST_BUFFER_COMMON_H__ */
