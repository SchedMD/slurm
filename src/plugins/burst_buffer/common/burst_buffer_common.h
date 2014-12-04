/*****************************************************************************\
 *  burst_buffer_common.h - Common header for managing burst_buffers
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC.
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
	uint32_t job_size_limit;
	uint32_t prio_boost_alloc;
	uint32_t prio_boost_use;
	uint32_t stage_in_timeout;
	uint32_t stage_out_timeout;
	char    *start_stage_in;
	char    *start_stage_out;
	char    *stop_stage_in;
	char    *stop_stage_out;
	uint32_t user_size_limit;
} bb_config_t;

typedef struct bb_alloc {
	uint32_t array_job_id;
	uint32_t array_task_id;
	bool cancelled;
	time_t end_time;	/* Expected time when use will end */
	uint32_t job_id;
	char *name;		/* For persistent burst buffers */
	struct bb_alloc *next;
	time_t seen_time;	/* Time buffer last seen */
	uint32_t size;
	uint16_t state;
	time_t state_time;	/* Time of last state change */
	time_t use_time;	/* Expected time when use will begin */
	uint32_t user_id;
} bb_alloc_t;

typedef struct bb_user {
	struct bb_user *next;
	uint32_t size;
	uint32_t user_id;
} bb_user_t;

typedef struct job_queue_rec {
	uint32_t bb_size;
	struct job_record *job_ptr;
} job_queue_rec_t;

struct preempt_bb_recs {
	bb_alloc_t *bb_ptr;
	uint32_t job_id;
	uint32_t size;
	time_t   use_time;
	uint32_t user_id;
};

/* Allocate burst buffer hash tables */
extern void bb_alloc_cache(bb_alloc_t ***bb_hash_ptr,bb_user_t ***bb_uhash_ptr);

/* Clear all cached burst buffer records, freeing all memory. */
extern void bb_clear_cache(bb_alloc_t ***bb_hash_ptr,bb_user_t ***bb_uhash_ptr);

/* Clear configuration parameters, free memory */
extern void bb_clear_config(bb_config_t *config_ptr);

/* Find a per-job burst buffer record for a specific job.
 * If not found, return NULL. */
extern bb_alloc_t *bb_find_job_rec(struct job_record *job_ptr,
				   bb_alloc_t **bb_hash);

/* Find a per-user burst buffer record for a specific user ID */
extern bb_user_t *bb_find_user_rec(uint32_t user_id, bb_user_t **bb_uhash);

/* Translate a burst buffer size specification in string form to numeric form,
 * recognizing various sufficies (MB, GB, TB, PB, and Nodes). */
extern uint32_t bb_get_size_num(char *tok);

extern void bb_job_queue_del(void *x);

/* Sort job queue by expected start time */
extern int bb_job_queue_sort(void *x, void *y);

/* Load and process configuration parameters */
extern void bb_load_config(bb_config_t *config_ptr);

/* Pack individual burst buffer records into a  buffer */
extern int bb_pack_bufs(bb_alloc_t **bb_hash, Buf buffer,
			uint16_t protocol_version);

/* Pack configuration parameters into a buffer */
extern void bb_pack_config(bb_config_t *config_ptr, Buf buffer,
			   uint16_t protocol_version);

/* Sort preempt_bb_recs in order of DECREASING use_time */
extern int bb_preempt_queue_sort(void *x, void *y);

/* Remove a burst buffer allocation from a user's load */
extern void bb_remove_user_load(bb_alloc_t *bb_ptr, uint32_t *used_space,
				bb_user_t **bb_uhash);

#endif	/* __BURST_BUFFER_COMMON_H__ */
