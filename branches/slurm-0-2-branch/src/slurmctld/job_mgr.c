/*****************************************************************************\
 *  job_mgr.c - manage the job information of slurm
 *	Note: there is a global job list (job_list), job_count, time stamp 
 *	(last_job_update), and hash table (job_hash, job_hash_over, 
 *	max_hash_over)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_ELAN
#  include "src/common/qsw.h"
#  define BUF_SIZE (1024 + QSW_PACK_SIZE)
#else
#  define BUF_SIZE 1024
#endif

#include <slurm/slurm_errno.h>

#include "src/common/hostlist.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#define DETAILS_FLAG 0xdddd
#define MAX_NODE_FRAGMENTS 8
#define MAX_RETRIES  10
#define SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 0
#define STEP_FLAG 0xbbbb
#define TOP_PRIORITY 0xffff0000	/* large, but leave headroom for higher */

#define JOB_HASH_INX(_job_id)	(_job_id % hash_table_size)

/* Global variables */
List job_list = NULL;		/* job_record list */
time_t last_job_update;		/* time of last update to job records */

/* Local variables */
static int    default_prio = TOP_PRIORITY;
static int    hash_table_size = 0;
static int    job_count = 0;        /* job's in the system */
static long   job_id_sequence = -1; /* first job_id to assign new job */
static struct job_record **job_hash = NULL;
static struct job_record **job_hash_over = NULL;
static int    max_hash_over = 0;

/* Local functions */
static void _add_job_hash(struct job_record *job_ptr);
static int  _copy_job_desc_to_file(job_desc_msg_t * job_desc,
				   uint32_t job_id);
static int  _copy_job_desc_to_job_record(job_desc_msg_t * job_desc,
					 struct job_record **job_ptr,
					 struct part_record *part_ptr,
					 bitstr_t ** exc_bitmap,
					 bitstr_t ** req_bitmap);
static char *_copy_nodelist_no_dup(char *node_list);
static void _del_batch_list_rec(void *x);
static void _delete_job_desc_files(uint32_t job_id);
static void _dump_job_details(struct job_details *detail_ptr,
				    Buf buffer);
static void _dump_job_state(struct job_record *dump_job_ptr, Buf buffer);
static void _dump_job_step_state(struct step_record *step_ptr, Buf buffer);
static void _excise_node_from_job(struct job_record *job_ptr, 
				  struct node_record *node_ptr);
static int  _find_batch_dir(void *x, void *key);
static void _get_batch_job_dir_ids(List batch_dirs);
static void _job_timed_out(struct job_record *job_ptr);
static int  _job_create(job_desc_msg_t * job_specs, uint32_t * new_job_id,
		        int allocate, int will_run,
		        struct job_record **job_rec_ptr, uid_t submit_uid);
static void _kill_job_on_node(uint32_t job_id, struct node_record *node_ptr);
static void _list_delete_job(void *job_entry);
static int  _list_find_job_id(void *job_entry, void *key);
static int  _list_find_job_old(void *job_entry, void *key);
static int  _load_job_details(struct job_record *job_ptr, Buf buffer);
static int  _load_job_state(Buf buffer);
static int  _load_step_state(struct job_record *job_ptr, Buf buffer);
static void _pack_job_details(struct job_details *detail_ptr, Buf buffer);
static int  _purge_job_record(uint32_t job_id);
static void _purge_lost_batch_jobs(int node_inx, time_t now);
static void _read_data_array_from_file(char *file_name, char ***data,
				       uint16_t * size);
static void _read_data_from_file(char *file_name, char **data);
static void _remove_defunct_batch_dirs(List batch_dirs);
static int  _reset_detail_bitmaps(struct job_record *job_ptr);
static void _reset_step_bitmaps(struct job_record *job_ptr);
static void _set_job_id(struct job_record *job_ptr);
static void _set_job_prio(struct job_record *job_ptr);
static bool _slurm_picks_nodes(job_desc_msg_t * job_specs);
static bool _top_priority(struct job_record *job_ptr);
static int  _validate_job_create_req(job_desc_msg_t * job_desc);
static int  _validate_job_desc(job_desc_msg_t * job_desc_msg, int allocate,
				uid_t submit_uid);
static void _validate_job_files(List batch_dirs);
static int  _write_data_to_file(char *file_name, char *data);
static int  _write_data_array_to_file(char *file_name, char **data,
				     uint16_t size);
static void _xmit_new_end_time(struct job_record *job_ptr);

/* 
 * create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * IN/OUT error_code - set to zero if no error, errno otherwise
 * RET pointer to the record or NULL if error
 * global: job_list - global job list
 *	job_count - number of jobs in the system
 *	last_job_update - time of last job table update
 * NOTE: allocates memory that should be xfreed with _list_delete_job
 */
struct job_record *create_job_record(int *error_code)
{
	struct job_record *job_record_point;
	struct job_details *job_details_point;

	if (job_count >= slurmctld_conf.max_job_cnt) {
		error("create_job_record: job_count exceeds limit");
		*error_code = EAGAIN;
		return NULL;
	}

	job_count++;
	*error_code = 0;
	last_job_update = time(NULL);

	job_record_point =
	    (struct job_record *) xmalloc(sizeof(struct job_record));
	job_details_point =
	    (struct job_details *) xmalloc(sizeof(struct job_details));

	xassert (job_record_point->magic = JOB_MAGIC); /* sets value */
	job_record_point->details = job_details_point;
	job_record_point->step_list = list_create(NULL);
	if (job_record_point->step_list == NULL)
		fatal("memory allocation failure");

	xassert (job_details_point->magic = DETAILS_MAGIC); /* set value */
	job_details_point->submit_time = time(NULL);

	if (list_append(job_list, job_record_point) == 0)
		fatal("list_append memory allocation failure");

	return job_record_point;
}


/* 
 * delete_job_details - delete a job's detail record and clear it's pointer
 *	this information can be deleted as soon as the job is allocated  
 *	resources and running (could need to restart batch job)
 * IN job_entry - pointer to job_record to clear the record of
 */
void delete_job_details(struct job_record *job_entry)
{
	if (job_entry->details == NULL)
		return;

	_delete_job_desc_files(job_entry->job_id);
	xassert (job_entry->details->magic == DETAILS_MAGIC);
	xfree(job_entry->details->req_nodes);
	xfree(job_entry->details->exc_nodes);
	FREE_NULL_BITMAP(job_entry->details->req_node_bitmap);
	FREE_NULL_BITMAP(job_entry->details->exc_node_bitmap);
	xfree(job_entry->details->features);
	xfree(job_entry->details->err);
	xfree(job_entry->details->in);
	xfree(job_entry->details->out);
	xfree(job_entry->details->work_dir);
	xfree(job_entry->details);
}

/* _delete_job_desc_files - delete job descriptor related files */
static void _delete_job_desc_files(uint32_t job_id)
{
	char *dir_name, job_dir[20], *file_name;
	struct stat sbuf;

	dir_name = xstrdup(slurmctld_conf.state_save_location);

	sprintf(job_dir, "/job.%d", job_id);
	xstrcat(dir_name, job_dir);

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/environment");
	(void) unlink(file_name);
	xfree(file_name);

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/script");
	(void) unlink(file_name);
	xfree(file_name);

	if (stat(dir_name, &sbuf) == 0)	/* remove job directory as needed */
		(void) rmdir(dir_name);
	xfree(dir_name);
}

/* dump_all_job_state - save the state of all jobs to file for checkpoint
 * RET 0 or error code */
int dump_all_job_state(void)
{
	int error_code = 0, log_fd;
	char *old_file, *new_file, *reg_file;
	/* Locks: Read config and job */
	slurmctld_lock_t job_read_lock =
	    { READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	Buf buffer = init_buf(BUF_SIZE * 16);

	/* write header: time */
	pack_time(time(NULL), buffer);

	/* write individual job records */
	lock_slurmctld(job_read_lock);
	job_record_iterator = list_iterator_create(job_list);
	while ((job_record_point =
		(struct job_record *) list_next(job_record_iterator))) {
		xassert (job_record_point->magic == JOB_MAGIC);
		_dump_job_state(job_record_point, buffer);
	}
	unlock_slurmctld(job_read_lock);
	list_iterator_destroy(job_record_iterator);

	/* write the buffer to file */
	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/job_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/job_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/job_state.new");
	lock_state_files();
	log_fd = creat(new_file, 0600);
	if (log_fd == 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		if (write
		    (log_fd, get_buf_data(buffer),
		     get_buf_offset(buffer)) != get_buf_offset(buffer)) {
			error("Can't save state, write file %s error %m",
			      new_file);
			error_code = errno;
		}
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		(void) link(reg_file, old_file);
		(void) unlink(reg_file);
		(void) link(new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	unlock_state_files();

	free_buf(buffer);
	return error_code;
}

/*
 * load_all_job_state - load the job state from file, recover from last 
 *	checkpoint. Execute this after loading the configuration file data.
 * RET 0 or error code
 */
int load_all_job_state(void)
{
	int data_allocated, data_read = 0, error_code = 0;
	uint32_t data_size = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;

	/* read the file */
	state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(state_file, "/job_state");
	lock_state_files();
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		info("No job state file (%s) to recover", state_file);
		error_code = ENOENT;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while ((data_read =
			read(state_fd, &data[data_size],
			     BUF_SIZE)) == BUF_SIZE) {
			data_size += data_read;
			data_allocated += BUF_SIZE;
			xrealloc(data, data_allocated);
		}
		data_size += data_read;
		close(state_fd);
		if (data_read < 0)
			error("Error reading file %s: %m", state_file);
	}
	xfree(state_file);
	unlock_state_files();

	if (job_id_sequence < 0)
		job_id_sequence = slurmctld_conf.first_job_id;

	buffer = create_buf(data, data_size);
	safe_unpack_time(&buf_time, buffer);

	while (remaining_buf(buffer) > 0) {
		error_code = _load_job_state(buffer);
		if (error_code != SLURM_SUCCESS)
			goto unpack_error;
	}

	free_buf(buffer);
	return error_code;

      unpack_error:
	error("Incomplete job data checkpoint file");
	error("Job state not completely restored");
	free_buf(buffer);
	return SLURM_FAILURE;
}

/*
 * _dump_job_state - dump the state of a specific job, its details, and 
 *	steps to a buffer
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void _dump_job_state(struct job_record *dump_job_ptr, Buf buffer)
{
	struct job_details *detail_ptr;
	ListIterator step_record_iterator;
	struct step_record *step_record_ptr;

	/* Dump basic job info */
	pack32(dump_job_ptr->job_id, buffer);
	pack32(dump_job_ptr->user_id, buffer);
	pack32(dump_job_ptr->time_limit, buffer);
	pack32(dump_job_ptr->priority, buffer);
	pack32(dump_job_ptr->alloc_sid, buffer);

	pack_time(dump_job_ptr->start_time, buffer);
	pack_time(dump_job_ptr->end_time, buffer);

	pack16((uint16_t) dump_job_ptr->job_state, buffer);
	pack16(dump_job_ptr->next_step_id, buffer);
	pack16(dump_job_ptr->kill_on_node_fail, buffer);
	pack16(dump_job_ptr->kill_on_step_done, buffer);
	pack16(dump_job_ptr->batch_flag, buffer);

	packstr(dump_job_ptr->nodes, buffer);
	packstr(dump_job_ptr->partition, buffer);
	packstr(dump_job_ptr->name, buffer);
	packstr(dump_job_ptr->alloc_node, buffer);

	/* Dump job details, if available */
	detail_ptr = dump_job_ptr->details;
	if (detail_ptr) {
		xassert (detail_ptr->magic == DETAILS_MAGIC);
		pack16((uint16_t) DETAILS_FLAG, buffer);
		_dump_job_details(detail_ptr, buffer);
	} else
		pack16((uint16_t) 0, buffer);	/* no details flag */

	/* Dump job steps */
	step_record_iterator =
	    list_iterator_create(dump_job_ptr->step_list);
	while ((step_record_ptr =
		(struct step_record *) list_next(step_record_iterator))) {
		pack16((uint16_t) STEP_FLAG, buffer);
		_dump_job_step_state(step_record_ptr, buffer);
	};
	list_iterator_destroy(step_record_iterator);
	pack16((uint16_t) 0, buffer);	/* no step flag */
}

/* Unpack a job's state information from a buffer */
static int _load_job_state(Buf buffer)
{
	uint32_t job_id, user_id, time_limit, priority, alloc_sid;
	time_t start_time, end_time;
	uint16_t job_state, next_step_id, details, batch_flag, step_flag;
	uint16_t kill_on_node_fail, kill_on_step_done, name_len;
	char *nodes = NULL, *partition = NULL, *name = NULL;
	char *alloc_node = NULL;
	bitstr_t *node_bitmap = NULL;
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	int error_code;

	safe_unpack32(&job_id, buffer);
	safe_unpack32(&user_id, buffer);
	safe_unpack32(&time_limit, buffer);
	safe_unpack32(&priority, buffer);
	safe_unpack32(&alloc_sid, buffer);

	safe_unpack_time(&start_time, buffer);
	safe_unpack_time(&end_time, buffer);

	safe_unpack16(&job_state, buffer);
	safe_unpack16(&next_step_id, buffer);
	safe_unpack16(&kill_on_node_fail, buffer);
	safe_unpack16(&kill_on_step_done, buffer);
	safe_unpack16(&batch_flag, buffer);

	safe_unpackstr_xmalloc(&nodes, &name_len, buffer);
	safe_unpackstr_xmalloc(&partition, &name_len, buffer);
	safe_unpackstr_xmalloc(&name, &name_len, buffer);
	safe_unpackstr_xmalloc(&alloc_node, &name_len, buffer);

	/* validity test as possible */
	if (((job_state & (~JOB_COMPLETING)) >= JOB_END) || 
	    (batch_flag > 1)) {
		error("Invalid data for job %u: job_state=%u batch_flag=%u",
		      job_id, job_state, batch_flag);
		goto unpack_error;
	}
	if (kill_on_step_done > KILL_ON_STEP_DONE) {
		error("Invalid data for job %u: kill_on_step_done=%u",
		      job_id, kill_on_step_done);
		goto unpack_error;
	}
	if (kill_on_node_fail > 1) {
		error("Invalid data for job %u: kill_on_node_fail=%u",
		      job_id, kill_on_node_fail);
		goto unpack_error;
	}
	if ((nodes) && (node_name2bitmap(nodes, &node_bitmap))) {
		error("_load_job_state: invalid nodes (%s) for job_id %u",
		      nodes, job_id);
		goto unpack_error;
	}
	part_ptr = list_find_first(part_list, &list_find_part,
				   partition);
	if (part_ptr == NULL) {
		error("Invalid partition (%s) for job_id %u", 
		     partition, job_id);
		goto unpack_error;
	}

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		job_ptr = create_job_record(&error_code);
		if (error_code) {
			error("Create job entry failed for job_id %u",
			      job_id);
			goto unpack_error;
		}
		job_ptr->job_id = job_id;
		_add_job_hash(job_ptr);
	}

	if ((default_prio >= priority) && (priority > 1))
		default_prio = priority - 1;
	if (job_id_sequence <= job_id)
		job_id_sequence = job_id + 1;

	safe_unpack16(&details, buffer);
	if ((details == DETAILS_FLAG) && 
	    (_load_job_details(job_ptr, buffer))) {
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time = time(NULL);
		goto unpack_error;
	}

	job_ptr->user_id      = user_id;
	job_ptr->time_limit   = time_limit;
	job_ptr->priority     = priority;
	job_ptr->alloc_sid    = alloc_sid;
	job_ptr->start_time   = start_time;
	job_ptr->end_time     = end_time;
	job_ptr->job_state    = job_state;
	job_ptr->next_step_id = next_step_id;
	job_ptr->time_last_active = time(NULL);
	strncpy(job_ptr->name, name, MAX_NAME_LEN);
	xfree(name);
	xfree(job_ptr->nodes);
	job_ptr->nodes  = nodes;
	nodes           = NULL;	/* reused, nothing left to free */
	xfree(job_ptr->alloc_node);
	job_ptr->alloc_node = alloc_node;
	alloc_node          = NULL;	/* reused, nothing left to free */
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	job_ptr->node_bitmap  = node_bitmap;
	node_bitmap           = NULL;
	strncpy(job_ptr->partition, partition, MAX_NAME_LEN);
	xfree(partition);
	job_ptr->part_ptr = part_ptr;
	job_ptr->kill_on_node_fail = kill_on_node_fail;
	job_ptr->kill_on_step_done = kill_on_step_done;
	job_ptr->batch_flag        = batch_flag;
	build_node_details(job_ptr);	/* set: num_cpu_groups, cpus_per_node, 
					 *	cpu_count_reps, node_cnt, and
					 *	node_addr */
	info("recovered job id %u", job_id);

	safe_unpack16(&step_flag, buffer);
	while (step_flag == STEP_FLAG) {
		if ((error_code = _load_step_state(job_ptr, buffer)))
			goto unpack_error;
		safe_unpack16(&step_flag, buffer);
	}

	return SLURM_SUCCESS;

      unpack_error:
	xfree(nodes);
	xfree(partition);
	xfree(name);
	xfree(alloc_node);
	FREE_NULL_BITMAP(node_bitmap);
	return SLURM_FAILURE;
}

/*
 * _dump_job_details - dump the state of a specific job details to 
 *	a buffer
 * IN detail_ptr - pointer to job details for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
void _dump_job_details(struct job_details *detail_ptr, Buf buffer)
{
	pack32((uint32_t) detail_ptr->num_procs, buffer);
	pack32((uint32_t) detail_ptr->min_nodes, buffer);
	pack32((uint32_t) detail_ptr->max_nodes, buffer);
	pack32((uint32_t) detail_ptr->total_procs, buffer);

	pack16((uint16_t) detail_ptr->shared, buffer);
	pack16((uint16_t) detail_ptr->contiguous, buffer);

	pack32((uint32_t) detail_ptr->min_procs, buffer);
	pack32((uint32_t) detail_ptr->min_memory, buffer);
	pack32((uint32_t) detail_ptr->min_tmp_disk, buffer);
	pack_time(detail_ptr->submit_time, buffer);

	packstr(detail_ptr->req_nodes, buffer);
	packstr(detail_ptr->exc_nodes, buffer);
	packstr(detail_ptr->features,  buffer);

	packstr(detail_ptr->err,       buffer);
	packstr(detail_ptr->in,        buffer);
	packstr(detail_ptr->out,       buffer);
	packstr(detail_ptr->work_dir,  buffer);
}

/* _load_job_details - Unpack a job details information from buffer */
static int _load_job_details(struct job_record *job_ptr, Buf buffer)
{
	char *req_nodes = NULL, *exc_nodes = NULL, *features = NULL;
	char *err = NULL, *in = NULL, *out = NULL, *work_dir = NULL;
	bitstr_t *req_node_bitmap = NULL, *exc_node_bitmap = NULL;
	uint32_t num_procs, min_nodes, max_nodes, min_procs;
	uint16_t shared, contiguous, name_len;
	uint32_t min_memory, min_tmp_disk, total_procs;
	time_t submit_time;

	/* unpack the job's details from the buffer */
	safe_unpack32(&num_procs, buffer);
	safe_unpack32(&min_nodes, buffer);
	safe_unpack32(&max_nodes, buffer);
	safe_unpack32(&total_procs, buffer);

	safe_unpack16(&shared, buffer);
	safe_unpack16(&contiguous, buffer);

	safe_unpack32(&min_procs, buffer);
	safe_unpack32(&min_memory, buffer);
	safe_unpack32(&min_tmp_disk, buffer);
	safe_unpack_time(&submit_time, buffer);

	safe_unpackstr_xmalloc(&req_nodes, &name_len, buffer);
	safe_unpackstr_xmalloc(&exc_nodes, &name_len, buffer);
	safe_unpackstr_xmalloc(&features,  &name_len, buffer);

	safe_unpackstr_xmalloc(&err, &name_len, buffer);
	safe_unpackstr_xmalloc(&in,  &name_len, buffer);
	safe_unpackstr_xmalloc(&out, &name_len, buffer);
	safe_unpackstr_xmalloc(&work_dir, &name_len, buffer);

	/* validity test as possible */
	if ((shared > 1) || (contiguous > 1)) {
		error("Invalid data for job %u: shared=%u contiguous=%u",
		      job_ptr->job_id, shared, contiguous);
		goto unpack_error;
	}
	if ((req_nodes) && (node_name2bitmap(req_nodes, &req_node_bitmap))) {
		error("Invalid req_nodes (%s) for job_id %u",
		      req_nodes, job_ptr->job_id);
		goto unpack_error;
	}
	if ((exc_nodes) && (node_name2bitmap(exc_nodes, &exc_node_bitmap))) {
		error("Invalid exc_nodes (%s) for job_id %u",
		      exc_nodes, job_ptr->job_id);
		goto unpack_error;
	}

	/* free any left-over detail data */
	xfree(job_ptr->details->req_nodes);
	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
	xfree(job_ptr->details->exc_nodes);
	FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
	xfree(job_ptr->details->features);
	xfree(job_ptr->details->err);
	xfree(job_ptr->details->in);
	xfree(job_ptr->details->out);
	xfree(job_ptr->details->work_dir);

	/* now put the details into the job record */
	job_ptr->details->num_procs = num_procs;
	job_ptr->details->min_nodes = min_nodes;
	job_ptr->details->max_nodes = max_nodes;
	job_ptr->details->total_procs = total_procs;
	job_ptr->details->shared = shared;
	job_ptr->details->contiguous = contiguous;
	job_ptr->details->min_procs = min_procs;
	job_ptr->details->min_memory = min_memory;
	job_ptr->details->min_tmp_disk = min_tmp_disk;
	job_ptr->details->submit_time = submit_time;
	job_ptr->details->req_nodes = req_nodes;
	job_ptr->details->req_node_bitmap = req_node_bitmap;
	job_ptr->details->exc_nodes = exc_nodes;
	job_ptr->details->exc_node_bitmap = exc_node_bitmap;
	job_ptr->details->features = features;
	job_ptr->details->err = err;
	job_ptr->details->in = in;
	job_ptr->details->out = out;
	job_ptr->details->work_dir = work_dir;

	return SLURM_SUCCESS;

      unpack_error:
	xfree(req_nodes);
	xfree(exc_nodes);
	FREE_NULL_BITMAP(req_node_bitmap);
	FREE_NULL_BITMAP(exc_node_bitmap);
	xfree(features);
	xfree(err);
	xfree(in);
	xfree(out);
	xfree(work_dir);
	return SLURM_FAILURE;
}


/*
 * _dump_job_step_state - dump the state of a specific job step to a buffer
 * IN detail_ptr - pointer to job step for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void _dump_job_step_state(struct step_record *step_ptr, Buf buffer)
{
	pack16((uint16_t) step_ptr->step_id, buffer);
	pack16((uint16_t) step_ptr->cyclic_alloc, buffer);
	pack32(step_ptr->num_tasks, buffer);
	pack_time(step_ptr->start_time, buffer);

	packstr(step_ptr->step_node_list,  buffer);
#ifdef HAVE_ELAN
	qsw_pack_jobinfo(step_ptr->qsw_job, buffer);
#endif
}

/* Unpack job step state information from a buffer */
static int _load_step_state(struct job_record *job_ptr, Buf buffer)
{
	struct step_record *step_ptr;
	uint16_t step_id, cyclic_alloc, name_len;
	uint32_t num_tasks;
	time_t start_time;
	char *step_node_list = NULL;

	safe_unpack16(&step_id, buffer);
	safe_unpack16(&cyclic_alloc, buffer);
	safe_unpack32(&num_tasks, buffer);
	safe_unpack_time(&start_time, buffer);
	safe_unpackstr_xmalloc(&step_node_list, &name_len, buffer);

	/* validity test as possible */
	if (cyclic_alloc > 1) {
		error("Invalid data for job %u.%u: cyclic_alloc=%u",
		      job_ptr->job_id, step_id, cyclic_alloc);
		goto unpack_error;
	}

	step_ptr = find_step_record(job_ptr, step_id);
	if (step_ptr == NULL)
		step_ptr = create_step_record(job_ptr);
	if (step_ptr == NULL)
		return SLURM_FAILURE;

	/* free any left-over values */
	xfree(step_ptr->step_node_list);
	FREE_NULL_BITMAP(step_ptr->step_node_bitmap);

	/* set new values */
	step_ptr->step_id      = step_id;
	step_ptr->cyclic_alloc = cyclic_alloc;
	step_ptr->num_tasks    = num_tasks;
	step_ptr->start_time   = start_time;
	step_ptr->step_node_list = step_node_list;
	if (step_node_list)
		(void) node_name2bitmap(step_node_list, 
					&(step_ptr->step_node_bitmap));
	step_node_list = NULL;	/* re-used, nothing left to free */
#ifdef HAVE_ELAN
	qsw_alloc_jobinfo(&step_ptr->qsw_job);
	if (qsw_unpack_jobinfo(step_ptr->qsw_job, buffer)) {
		qsw_free_jobinfo(step_ptr->qsw_job);
		goto unpack_error;
	}
#endif
	info("recovered job step %u.%u", job_ptr->job_id, step_id);
	return SLURM_SUCCESS;

      unpack_error:
	xfree(step_node_list);
	return SLURM_FAILURE;
}

/* _add_job_hash - add a job hash entry for given job record, job_id must  
 *	already be set
 * IN job_ptr - pointer to job record
 * Globals: hash table updated
 */
void _add_job_hash(struct job_record *job_ptr)
{
	int inx;

	inx = JOB_HASH_INX(job_ptr->job_id);
	if (job_hash[inx]) {
		if (max_hash_over >= hash_table_size)
			fatal("Job hash table overflow");
		job_hash_over[max_hash_over++] = job_ptr;
	} else
		job_hash[inx] = job_ptr;
}


/* 
 * find_job_record - return a pointer to the job record with the given job_id
 * IN job_id - requested job's id
 * RET pointer to the job's record, NULL on error
 * global: job_list - global job list pointer
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */
struct job_record *find_job_record(uint32_t job_id)
{
	int i;

	/* First try to find via hash table */
	if (job_hash[JOB_HASH_INX(job_id)] &&
	    job_hash[JOB_HASH_INX(job_id)]->job_id == job_id)
		return job_hash[JOB_HASH_INX(job_id)];

	/* linear search of overflow hash table overflow */
	for (i = 0; i < max_hash_over; i++) {
		if (job_hash_over[i] != NULL &&
		    job_hash_over[i]->job_id == job_id)
			return job_hash_over[i];
	}

	return NULL;
}

/*
 * find_running_job_by_node_name - Given a node name, return a pointer to any 
 *	job currently running on that node
 * IN node_name - name of a node
 * RET pointer to the job's record, NULL if no job on node found
 */
struct job_record *find_running_job_by_node_name(char *node_name)
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	struct node_record *node_record_point;
	int bit_position;

	node_record_point = find_node_record(node_name);
	if (node_record_point == NULL)	/* No such node */
		return NULL;
	bit_position = node_record_point - node_record_table_ptr;

	job_record_iterator = list_iterator_create(job_list);
	while ((job_record_point =
		(struct job_record *) list_next(job_record_iterator))) {
		if (job_record_point->job_state != JOB_RUNNING)
			continue;	/* job not active */
		if (bit_test(job_record_point->node_bitmap, bit_position))
			break;	/* found job here */
	}
	list_iterator_destroy(job_record_iterator);

	return job_record_point;
}

/*
 * kill_running_job_by_node_name - Given a node name, deallocate RUNNING 
 *	or COMPLETING jobs from the node or kill them 
 * IN node_name - name of a node
 * IN step_test - if true, only kill the job if a step is running on the node
 * RET number of killed jobs
 */
int kill_running_job_by_node_name(char *node_name, bool step_test)
{
	ListIterator job_record_iterator;
	struct job_record *job_ptr;
	struct node_record *node_ptr;
	int bit_position;
	int job_count = 0;

	node_ptr = find_node_record(node_name);
	if (node_ptr == NULL)	/* No such node */
		return 0;
	bit_position = node_ptr - node_record_table_ptr;

	job_record_iterator = list_iterator_create(job_list);
	while ((job_ptr =
		(struct job_record *) list_next(job_record_iterator))) {
		if ((job_ptr->node_bitmap == NULL) ||
		    (!bit_test(job_ptr->node_bitmap, bit_position)))
			continue;	/* job not on this node */
		if (job_ptr->job_state & JOB_COMPLETING) {
			job_count++;
			bit_clear(job_ptr->node_bitmap, bit_position);
			if (job_ptr->node_cnt)
				(job_ptr->node_cnt)--;
			else
				error("node_cnt underflow on JobId=%u", 
			   	      job_ptr->job_id);
			if (job_ptr->node_cnt == 0)
				job_ptr->job_state &= (~JOB_COMPLETING);
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			else
				error("Node %s comp_job_cnt underflow, JobId=%u", 
				      node_ptr->name, job_ptr->job_id);
		} else if (job_ptr->job_state == JOB_RUNNING) {
			if (step_test && 
			    (step_on_node(job_ptr, node_ptr) == 0))
				continue;

			job_count++;
			if ((job_ptr->details == NULL) ||
			    (job_ptr->kill_on_node_fail) ||
			    (job_ptr->node_cnt <= 1)) {
				error("Killing job_id %u on failed node %s",
				      job_ptr->job_id, node_name);
				job_ptr->job_state = JOB_NODE_FAIL | 
						     JOB_COMPLETING;
				job_ptr->end_time = time(NULL);
				deallocate_nodes(job_ptr, false);
				delete_all_step_records(job_ptr);
			} else {
				error("Removing failed node %s from job_id %u",
				      node_name, job_ptr->job_id);
				_excise_node_from_job(job_ptr, node_ptr);
			}
		}

	}
	list_iterator_destroy(job_record_iterator);
	if (job_count)
		last_job_update = time(NULL);

	return job_count;
}

/* Remove one node from a job's allocation */
static void _excise_node_from_job(struct job_record *job_ptr, 
				  struct node_record *node_ptr)
{
	make_node_idle(node_ptr, job_ptr); /* updates bitmap */
	job_ptr->nodes = bitmap2node_name(job_ptr->node_bitmap);
	xfree(job_ptr->cpus_per_node);
	xfree(job_ptr->cpu_count_reps);
	xfree(job_ptr->node_addr);

	/* build_node_details rebuilds everything from node_bitmap */
	build_node_details(job_ptr);
}


/*
 * dump_job_desc - dump the incoming job submit request message
 * IN job_specs - job specification from RPC
 */
void dump_job_desc(job_desc_msg_t * job_specs)
{
	long job_id, min_procs, min_memory, min_tmp_disk, num_procs;
	long min_nodes, max_nodes, time_limit, priority, contiguous;
	long kill_on_node_fail, shared, task_dist, immediate;

	if (job_specs == NULL)
		return;

	job_id = (job_specs->job_id != NO_VAL) ? 
			(long) job_specs->job_id : -1L;
	debug3("JobDesc: user_id=%u job_id=%ld partition=%s name=%s",
	       job_specs->user_id, job_id,
	       job_specs->partition, job_specs->name);

	min_procs    = (job_specs->min_procs != NO_VAL) ? 
			(long) job_specs->min_procs : -1L;
	min_memory   = (job_specs->min_memory != NO_VAL) ? 
			(long) job_specs->min_memory : -1L;
	min_tmp_disk = (job_specs->min_tmp_disk != NO_VAL) ? 
			(long) job_specs->min_tmp_disk : -1L;
	debug3
	    ("   min_procs=%ld min_memory=%ld min_tmp_disk=%ld features=%s",
	     min_procs, min_memory, min_tmp_disk, job_specs->features);

	num_procs = (job_specs->num_procs != NO_VAL) ? 
			(long) job_specs->num_procs : -1L;
	min_nodes = (job_specs->min_nodes != NO_VAL) ? 
			(long) job_specs->min_nodes : -1L;
	max_nodes = (job_specs->max_nodes != NO_VAL) ? 
			(long) job_specs->max_nodes : -1L;
	immediate = (job_specs->immediate == 0) ? 0L : 1L;
	debug3("   num_procs=%ld min_nodes=%ld max_nodes=%ld immediate=%ld",
	       num_procs, min_nodes, max_nodes, immediate);

	debug3("   req_nodes=%s exc_nodes=%s", 
	       job_specs->req_nodes, job_specs->exc_nodes);

	time_limit = (job_specs->time_limit != NO_VAL) ? 
			(long) job_specs->time_limit : -1L;
	priority   = (job_specs->priority != NO_VAL) ? 
			(long) job_specs->priority : -1L;
	contiguous = (job_specs->contiguous != (uint16_t) NO_VAL) ? 
			(long) job_specs->contiguous : -1L;
	shared = (job_specs->shared != (uint16_t) NO_VAL) ? 
			(long) job_specs->shared : -1L;
	debug3("   time_limit=%ld priority=%ld contiguous=%ld shared=%ld",
	       time_limit, priority, contiguous, shared);

	kill_on_node_fail = (job_specs->kill_on_node_fail != 
			     (uint16_t) NO_VAL) ? 
			(long) job_specs->kill_on_node_fail : -1L;
	task_dist = (job_specs->task_dist != (uint16_t) NO_VAL) ? 
			(long) job_specs->task_dist : -1L;
	debug3("   kill_on_node_fail=%ld task_dist=%ld script=%.40s...",
	       kill_on_node_fail, task_dist, job_specs->script);

	if (job_specs->env_size == 1)
		debug3("   environment=\"%s\"", job_specs->environment[0]);
	else if (job_specs->env_size == 2)
		debug3("   environment=%s,%s",
		       job_specs->environment[0],
		       job_specs->environment[1]);
	else if (job_specs->env_size > 2)
		debug3("   environment=%s,%s,%s,...",
		       job_specs->environment[0],
		       job_specs->environment[1],
		       job_specs->environment[2]);

	debug3("   in=%s out=%s err=%s",
	       job_specs->in, job_specs->out, job_specs->err);

	debug3("   work_dir=%s alloc_node:sid=%s:%u",
	       job_specs->work_dir,
	       job_specs->alloc_node, job_specs->alloc_sid);

}


/* 
 * init_job_conf - initialize the job configuration tables and values. 
 *	this should be called after creating node information, but 
 *	before creating any job entries. Pre-existing job entries are 
 *	left unchanged. 
 *	NOTE: The job hash table size does not change after initial creation.
 * RET 0 if no error, otherwise an error code
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
int init_job_conf(void)
{
	if (job_list == NULL) {
		job_count = 0;
		job_list = list_create(&_list_delete_job);
		if (job_list == NULL)
			fatal ("Memory allocation failure");
	}

	last_job_update = time(NULL);
	return SLURM_SUCCESS;
}

/* rehash_jobs - Create or rebuild the job rehash table. Actually for now we 
 * just preserve it */
void rehash_jobs(void)
{
	if (job_hash == NULL) {
		hash_table_size = slurmctld_conf.max_job_cnt;
		job_hash = (struct job_record **) xmalloc(hash_table_size *
					sizeof(struct job_record *));
		job_hash_over = (struct job_record **) xmalloc(hash_table_size *
					sizeof(struct job_record *));
	} else if (hash_table_size < slurmctld_conf.max_job_cnt) {
		/* If the MaxJobCount grows by too much, the hash table will 
		 * be ineffective without rebuilding. We don't presently bother
		 * to rebuild the hash table, but cut MaxJobCount back as 
		 * needed. */ 
		error ("MaxJobCount reset too high, restart slurmctld");
		slurmctld_conf.max_job_cnt = hash_table_size;
	}
}

/*
 * job_allocate - create job_records for the suppied job specification and 
 *	allocate nodes for it.
 * IN job_specs - job specifications
 * IN node_list - location for storing new job's allocated nodes
 * IN immediate - if set then either initiate the job immediately or fail
 * IN will_run - don't initiate the job if set, just test if it could run 
 *	now or later
 * IN allocate - resource allocation request if set, not a full job
 * OUT new_job_id - the new job's ID
 * OUT num_cpu_groups - number of cpu groups (elements in cpus_per_node 
 *	and cpu_count_reps)
 * OUT cpus_per_node - pointer to array of numbers of cpus on each node 
 *	allocate
 * OUT cpu_count_reps - pointer to array of numbers of consecutive nodes 
 *	having same cpu count
 * OUT node_list - list of nodes allocated to the job
 * OUT node_cnt - number of allocated nodes
 * OUT node_addr - slurm_addr's for the allocated nodes
 * RET 0 or an error code
 * NOTE: If allocating nodes lx[0-7] to a job and those nodes have cpu counts  
 *	 of 4, 4, 4, 4, 8, 8, 4, 4 then num_cpu_groups=3, cpus_per_node={4,8,4}
 *	and cpu_count_reps={4,2,2}
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 */
int job_allocate(job_desc_msg_t * job_specs, uint32_t * new_job_id,
		 char **node_list, uint16_t * num_cpu_groups,
		 uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps,
		 int immediate, int will_run, int allocate,
		 uid_t submit_uid, uint16_t * node_cnt,
		 slurm_addr ** node_addr)
{
	int error_code;
	bool no_alloc, top_prio, test_only;
	struct job_record *job_ptr;
#ifdef HAVE_ELAN
	bool pick_nodes = _slurm_picks_nodes(job_specs);
#endif
	error_code = _job_create(job_specs, new_job_id, allocate, will_run,
				 &job_ptr, submit_uid);
	if (error_code) {
		if (immediate && job_ptr) {
			job_ptr->job_state = JOB_FAILED;
			job_ptr->start_time = 0;
			job_ptr->end_time = 0;
		}
		return error_code;
	}
	if (job_ptr == NULL)
		fatal("job_allocate: allocated job %u lacks record",
		      new_job_id);

	top_prio = _top_priority(job_ptr);
#ifdef HAVE_ELAN
	/* Avoid resource fragmentation if important */
	if (top_prio && pick_nodes && job_is_completing())
		top_prio = false;	/* Don't scheduled job right now */
#endif
	if (immediate && (!top_prio)) {
		job_ptr->job_state  = JOB_FAILED;
		job_ptr->start_time = 0;
		job_ptr->end_time   = 0;
		return ESLURM_NOT_TOP_PRIORITY;
	}

	test_only = will_run || (allocate == 0);
	if (!test_only) {
		/* Some of these pointers are NULL on submit */
		if (num_cpu_groups)
			*num_cpu_groups = 0;
		if (node_list)
			*node_list = NULL;
		if (cpus_per_node)
			*cpus_per_node = NULL;
		if (cpu_count_reps)
			*cpu_count_reps = NULL;
		if (node_cnt)
			*node_cnt = 0;
		if (node_addr)
			*node_addr = (slurm_addr *) NULL;
		last_job_update = time(NULL);
	}

	no_alloc = test_only || (!top_prio);

	error_code = select_nodes(job_ptr, no_alloc);
	if ((error_code == ESLURM_NODES_BUSY) ||
	    (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE)) {
		/* Not fatal error, but job can't be scheduled right now */
		if (immediate) {
			job_ptr->job_state  = JOB_FAILED;
			job_ptr->start_time = 0;
			job_ptr->end_time   = 0;
		} else		/* job remains queued */
			if (error_code == ESLURM_NODES_BUSY) 
				error_code = SLURM_SUCCESS;
		return error_code;
	}

	if (error_code) {	/* fundamental flaw in job request */
		job_ptr->job_state  = JOB_FAILED;
		job_ptr->start_time = 0;
		job_ptr->end_time   = 0;
		return error_code;
	}

	if (will_run) {		/* job would run, flag job destruction */
		job_ptr->job_state  = JOB_FAILED;
		job_ptr->start_time = 0;
		job_ptr->end_time   = 0;
	}

	if (!no_alloc) {
		if (node_list)
			*node_list = job_ptr->nodes;
		if (num_cpu_groups)
			*num_cpu_groups = job_ptr->num_cpu_groups;
		if (cpus_per_node)
			*cpus_per_node = job_ptr->cpus_per_node;
		if (cpu_count_reps)
			*cpu_count_reps = job_ptr->cpu_count_reps;
		if (node_cnt)
			*node_cnt = job_ptr->node_cnt;
		if (node_addr)
			*node_addr = job_ptr->node_addr;
	}

	return SLURM_SUCCESS;
}

/* Return true of slurm is performing the node selection process. 
 * This is a simplistic algorithm and does not count nodes. It just
 * looks for a required node list a no more than one required node/task. */
static bool _slurm_picks_nodes(job_desc_msg_t * job_specs)
{
	if (job_specs->req_nodes == NULL)
		return true;
	if ((job_specs->num_procs != NO_VAL) && (job_specs->num_procs > 1))
		return true;
	if ((job_specs->min_nodes != NO_VAL) && (job_specs->min_nodes > 1))
		return true;
	if ((job_specs->max_nodes != NO_VAL) && (job_specs->max_nodes > 1))
		return true;

	return false;
}

/* 
 * job_signal - signal the specified job
 * IN job_id - id of the job to be signaled
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN uid - uid of requesting user
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int job_signal(uint32_t job_id, uint16_t signal, uid_t uid)
{
	struct job_record *job_ptr;
	time_t now = time(NULL);

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("job_signal: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->user_id != uid) && (uid != 0) && (uid != getuid())) {
		error("Security violation, JOB_CANCEL RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	if ((job_ptr->job_state == JOB_PENDING) &&
	    (signal == SIGKILL)) {
		last_job_update		= now;
		job_ptr->job_state	= JOB_FAILED;
		job_ptr->start_time	= now;
		job_ptr->end_time	= now;
		delete_job_details(job_ptr);
		verbose("job_signal of pending job %u successful", job_id);
		return SLURM_SUCCESS;
	}

	if (job_ptr->job_state == JOB_RUNNING) {
		if (signal == SIGKILL) {
			/* No need to signal steps, deallocate kills them */
			job_ptr->time_last_active	= now;
			job_ptr->end_time		= now;
			last_job_update			= now;
			job_ptr->job_state = JOB_COMPLETE | JOB_COMPLETING;
			deallocate_nodes(job_ptr, false);
		} else {
			ListIterator step_record_iterator;
			struct step_record *step_ptr;

			step_record_iterator = 
				list_iterator_create (job_ptr->step_list);		
			while ((step_ptr = (struct step_record *)
					list_next (step_record_iterator))) {
				signal_step_tasks(step_ptr, signal);
			}
			list_iterator_destroy (step_record_iterator);
		}
		verbose("job_signal %u of running job %u successful", 
			signal, job_id);
		return SLURM_SUCCESS;
	}

	verbose("job_signal: job %u can't be sent signal %u from state=%s",
		job_id, signal, job_state_string(job_ptr->job_state));
	return ESLURM_TRANSITION_STATE_NO_UPDATE;
}

/* 
 * job_complete - note the normal termination the specified job
 * IN job_id - id of the job which completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN job_return_code - job's return code, if set then set state to FAILED
 * RET - 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int
job_complete(uint32_t job_id, uid_t uid, bool requeue,
	     uint32_t job_return_code)
{
	struct job_record *job_ptr;
	time_t now = time(NULL);
	uint32_t job_comp_flag = 0;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("job_complete: invalid JobId=%u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	if ((job_ptr->user_id != uid) && (uid != 0) && (uid != getuid())) {
		error("Security violation, JOB_COMPLETE RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (job_ptr->job_state == JOB_RUNNING)
		job_comp_flag = JOB_COMPLETING;
	if (requeue && job_ptr->details && job_ptr->batch_flag) {
		job_ptr->job_state = JOB_PENDING | job_comp_flag;
		info("Requeing job %u", job_ptr->job_id);
	} else if (job_ptr->job_state == JOB_PENDING) {
		job_ptr->job_state  = JOB_COMPLETE;
		job_ptr->start_time = 0;
		job_ptr->end_time   = 0;
	} else {
		if (job_return_code)
			job_ptr->job_state = JOB_FAILED   | job_comp_flag;
		else if (job_comp_flag &&		/* job was running */
			 (job_ptr->end_time < now))	/* over time limit */
			job_ptr->job_state = JOB_TIMEOUT  | job_comp_flag;
		else
			job_ptr->job_state = JOB_COMPLETE | job_comp_flag;
		job_ptr->end_time = now;
		delete_all_step_records(job_ptr);
	}

	last_job_update = now;
	if (job_comp_flag) {	/* job was running */
		deallocate_nodes(job_ptr, false);
		verbose("job_complete for JobId=%u successful", job_id);
	} else {
		verbose("job_complete for JobId=%u successful", job_id);
	}

	return SLURM_SUCCESS;
}

/*
 * _job_create - create a job table record for the supplied specifications.
 *	this performs only basic tests for request validity (access to 
 *	partition, nodes count in partition, and sufficient processors in 
 *	partition).
 * input: job_specs - job specifications
 * IN allocate - resource allocation request if set rather than job submit
 * IN will_run - job is not to be created, test of validity only
 * OUT new_job_id - the job's ID
 * OUT job_rec_ptr - pointer to the job (NULL on error)
 * RET 0 on success, otherwise ESLURM error code
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */

static int _job_create(job_desc_msg_t * job_desc, uint32_t * new_job_id,
		       int allocate, int will_run,
		       struct job_record **job_rec_ptr, uid_t submit_uid)
{
	int error_code = SLURM_SUCCESS, i;
	struct part_record *part_ptr;
	bitstr_t *req_bitmap = NULL, *exc_bitmap = NULL;

	*job_rec_ptr = (struct job_record *) NULL;
	if ((error_code = _validate_job_desc(job_desc, allocate, submit_uid)))
		return error_code;

	/* find selected partition */
	if (job_desc->partition) {
		part_ptr = list_find_first(part_list, &list_find_part,
					   job_desc->partition);
		if (part_ptr == NULL) {
			info("_job_create: invalid partition specified: %s", 
			     job_desc->partition);
			error_code = ESLURM_INVALID_PARTITION_NAME;
			return error_code;
		}
	} else {
		if (default_part_loc == NULL) {
			error("_job_create: default partition not set.");
			error_code = ESLURM_DEFAULT_PARTITION_NOT_SET;
			return error_code;
		}
		part_ptr = default_part_loc;
	}

	/* can this user access this partition */
	if ((part_ptr->root_only) && (submit_uid != 0)) {
		info("_job_create: uid %u access to partition %s denied, %s",
		     (unsigned int) submit_uid, part_ptr->name, "not root");
		error_code = ESLURM_ACCESS_DENIED;
		return error_code;
	}
	if (validate_group(part_ptr, submit_uid) == 0) {
		info("_job_create: uid %u access to partition %s denied, %s",
		     (unsigned int) submit_uid, part_ptr->name, "bad group");
		error_code = ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP;
		return error_code;
	}

	/* check if select partition has sufficient resources to satisfy
	 * the request */

	/* insure that selected nodes are in this partition */
	if (job_desc->req_nodes) {
		error_code = node_name2bitmap(job_desc->req_nodes, 
					      &req_bitmap);
		if (error_code == EINVAL) {
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup;
		}
		if (error_code != 0) {
			error_code = EAGAIN;	/* no memory */
			goto cleanup;
		}
		if (job_desc->contiguous)
			bit_fill_gaps(req_bitmap);
		if (bit_super_set(req_bitmap, part_ptr->node_bitmap) != 1) {
			info("_job_create: requested nodes %s not in partition %s",
			     job_desc->req_nodes, part_ptr->name);
			error_code = ESLURM_REQUESTED_NODES_NOT_IN_PARTITION;
			goto cleanup;
		}
		i = count_cpus(req_bitmap);
		if (i > job_desc->num_procs)
			job_desc->num_procs = i;
		i = bit_set_count(req_bitmap);
		if (i > job_desc->min_nodes)
			job_desc->min_nodes = i;
	}
	if (job_desc->exc_nodes) {
		error_code = node_name2bitmap(job_desc->exc_nodes, 
					      &exc_bitmap);
		if (error_code == EINVAL) {
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup;
		}
	}
	if ((exc_bitmap != NULL) && (req_bitmap != NULL)) {
		bitstr_t *tmp_bitmap = NULL;
		bitoff_t first_set;
		tmp_bitmap = bit_copy(exc_bitmap);
		bit_and(tmp_bitmap, req_bitmap);
		first_set = bit_ffs(tmp_bitmap);
		FREE_NULL_BITMAP(tmp_bitmap);
		if (first_set != -1) {
			info("Job's required and excluded node lists overlap");
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup;
		}
	}

	if (job_desc->min_nodes == NO_VAL)
		job_desc->min_nodes = 1;
	if (job_desc->max_nodes == NO_VAL)
		job_desc->max_nodes = 0;
	if (job_desc->num_procs > part_ptr->total_cpus) {
		info("Job requested too many cpus (%d) of partition %s(%d)", 
		     job_desc->num_procs, part_ptr->name, 
		     part_ptr->total_cpus);
		error_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
		goto cleanup;
	}
	if (job_desc->min_nodes > part_ptr->total_nodes) {
		info("Job requested too many nodes (%d) of partition %s(%d)", 
		     job_desc->min_nodes, part_ptr->name, 
		     part_ptr->total_nodes);
		error_code = ESLURM_TOO_MANY_REQUESTED_NODES;
		goto cleanup;
	}
	if (job_desc->max_nodes && 
	    (job_desc->max_nodes < job_desc->min_nodes)) {
		info("Job's max_nodes < min_nodes");
		error_code = ESLURM_TOO_MANY_REQUESTED_NODES;
		goto cleanup;
	}


	if ((error_code =_validate_job_create_req(job_desc)))
		goto cleanup;

	if (will_run) {
		error_code = SLURM_SUCCESS;
		goto cleanup;
	}

	if ((error_code = _copy_job_desc_to_job_record(job_desc,
						       job_rec_ptr,
						       part_ptr,
						       &req_bitmap,
						       &exc_bitmap))) {
		error_code = ESLURM_ERROR_ON_DESC_TO_RECORD_COPY;
		goto cleanup;
	}

	if (job_desc->script) {
		if ((error_code = _copy_job_desc_to_file(job_desc,
							 (*job_rec_ptr)->
							 job_id))) {
			(*job_rec_ptr)->job_state = JOB_FAILED;
			error_code = ESLURM_WRITING_TO_FILE;
			goto cleanup;
		}
		(*job_rec_ptr)->batch_flag = 1;
	} else
		(*job_rec_ptr)->batch_flag = 0;
	*new_job_id = (*job_rec_ptr)->job_id;

	/* Insure that requested partition is valid right now, 
	 * otherwise leave job queued and provide warning code */
	if (job_desc->min_nodes > part_ptr->max_nodes) {
		info("Job %u requested too many nodes (%d) of partition %s(%d)",
		     *new_job_id, job_desc->min_nodes, part_ptr->name, 
		     part_ptr->max_nodes);
		error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	} else if ((job_desc->max_nodes != 0) &&    /* no max_nodes for job */
		   (job_desc->max_nodes < part_ptr->min_nodes)) {
		info("Job %u requested too few nodes (%d) of partition %s(%d)",
		     *new_job_id, job_desc->max_nodes, 
		     part_ptr->name, part_ptr->min_nodes);
		error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	} else if (part_ptr->state_up == 0) {
		info("Job %u requested down partition %s", 
		     *new_job_id, part_ptr->name);
		error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	}

      cleanup:
	FREE_NULL_BITMAP(req_bitmap);
	FREE_NULL_BITMAP(exc_bitmap);
	return error_code;
}

/* Perform some size checks on strings we store to prevent
 * malicious user filling slurmctld's memory
 * RET 0 or error code */
static int _validate_job_create_req(job_desc_msg_t * job_desc)
{
	if (job_desc->err && (strlen(job_desc->err) > BUF_SIZE)) {
		info("_validate_job_create_req: strlen(err) too big (%d)",
		     strlen(job_desc->err));
		return ESLURM_PATHNAME_TOO_LONG;
	}
	if (job_desc->in && (strlen(job_desc->in) > BUF_SIZE)) {
		info("_validate_job_create_req: strlen(in) too big (%d)",
		     strlen(job_desc->in));
		return  ESLURM_PATHNAME_TOO_LONG;
	}
	if (job_desc->out && (strlen(job_desc->out) > BUF_SIZE)) {
		info("_validate_job_create_req: strlen(out) too big (%d)",
		     strlen(job_desc->out));
		return  ESLURM_PATHNAME_TOO_LONG;
	}
	if (job_desc->work_dir && (strlen(job_desc->work_dir) > BUF_SIZE)) {
		info("_validate_job_create_req: strlen(work_dir) too big (%d)",
		     strlen(job_desc->work_dir));
		return  ESLURM_PATHNAME_TOO_LONG;
	}
	return SLURM_SUCCESS;
}

/* _copy_job_desc_to_file - copy the job script and environment from the RPC  
 *	structure into a file */
static int
_copy_job_desc_to_file(job_desc_msg_t * job_desc, uint32_t job_id)
{
	int error_code = 0;
	char *dir_name, job_dir[20], *file_name;

	/* Create state_save_location directory */
	dir_name = xstrdup(slurmctld_conf.state_save_location);

	/* Create job_id specific directory */
	sprintf(job_dir, "/job.%d", job_id);
	xstrcat(dir_name, job_dir);
	if (mkdir(dir_name, 0700)) {
		error("mkdir(%s) error %m", dir_name);
		xfree(dir_name);
		return ESLURM_WRITING_TO_FILE;
	}

	/* Create environment file, and write data to it */
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/environment");
	error_code = _write_data_array_to_file(file_name,
					       job_desc->environment,
					       job_desc->env_size);
	xfree(file_name);

	if (error_code == 0) {
		/* Create script file */
		file_name = xstrdup(dir_name);
		xstrcat(file_name, "/script");
		error_code =
		    _write_data_to_file(file_name, job_desc->script);
		xfree(file_name);
	}

	xfree(dir_name);
	return error_code;
}

/*
 * Create file with specified name and write the supplied data array to it
 * IN file_name - file to create and write to
 * IN data - array of pointers to strings (e.g. env)
 * IN size - number of elements in data
 */
static int
_write_data_array_to_file(char *file_name, char **data, uint16_t size)
{
	int fd, i, pos, nwrite, amount;

	fd = creat(file_name, 0600);
	if (fd < 0) {
		error("Error creating file %s, %m", file_name);
		return ESLURM_WRITING_TO_FILE;
	}

	if (data == NULL)
		return SLURM_SUCCESS;

	amount = write(fd, &size, sizeof(uint16_t));
	if (amount < sizeof(uint16_t)) {
		error("Error writing file %s, %m", file_name);
		close(fd);
		return ESLURM_WRITING_TO_FILE;
	}

	for (i = 0; i < size; i++) {
		nwrite = strlen(data[i]) + 1;
		pos = 0;
		while (nwrite > 0) {
			amount = write(fd, &data[i][pos], nwrite);
			if (amount < 0) {
				error("Error writing file %s, %m",
				      file_name);
				close(fd);
				return ESLURM_WRITING_TO_FILE;
			}
			nwrite -= amount;
			pos += amount;
		}
	}

	close(fd);
	return SLURM_SUCCESS;
}

/*
 * Create file with specified name and write the supplied data array to it
 * IN file_name - file to create and write to
 * IN data - pointer to string
 */
static int _write_data_to_file(char *file_name, char *data)
{
	int fd, pos, nwrite, amount;

	if (data == NULL) {
		(void) unlink(file_name);
		return SLURM_SUCCESS;
	}

	fd = creat(file_name, 0600);
	if (fd < 0) {
		error("Error creating file %s, %m", file_name);
		return ESLURM_WRITING_TO_FILE;
	}

	nwrite = strlen(data) + 1;
	pos = 0;
	while (nwrite > 0) {
		amount = write(fd, &data[pos], nwrite);
		if (amount < 0) {
			error("Error writing file %s, %m", file_name);
			close(fd);
			return ESLURM_WRITING_TO_FILE;
		}
		nwrite -= amount;
		pos += amount;
	}
	close(fd);
	return SLURM_SUCCESS;
}

/*
 * get_job_env - return the environment variables and their count for a 
 *	given job
 * IN job_ptr - pointer to job for which data is required
 * OUT env_size - number of elements to read
 * RET point to array of string pointers containing environment variables
 */
char **get_job_env(struct job_record *job_ptr, uint16_t * env_size)
{
	char job_dir[30], *file_name, **environment = NULL;

	file_name = xstrdup(slurmctld_conf.state_save_location);
	sprintf(job_dir, "/job.%d/environment", job_ptr->job_id);
	xstrcat(file_name, job_dir);

	_read_data_array_from_file(file_name, &environment, env_size);

	xfree(file_name);
	return environment;
}

/* 
 * get_job_script - return the script for a given job
 * IN job_ptr - pointer to job for which data is required
 * RET point to string containing job script
 */
char *get_job_script(struct job_record *job_ptr)
{
	char job_dir[30], *file_name, *script = NULL;

	file_name = xstrdup(slurmctld_conf.state_save_location);
	sprintf(job_dir, "/job.%d/script", job_ptr->job_id);
	xstrcat(file_name, job_dir);

	_read_data_from_file(file_name, &script);

	xfree(file_name);
	return script;
}

/*
 * Read a collection of strings from a file
 * IN file_name - file to read from
 * OUT data - pointer to array of pointers to strings (e.g. env),
 *	must be xfreed when no longer needed
 * OUT size - number of elements in data
 */
static void
_read_data_array_from_file(char *file_name, char ***data, uint16_t * size)
{
	int fd, pos, buf_size, amount, i;
	char *buffer, **array_ptr;
	uint16_t rec_cnt;

	xassert(file_name);
	xassert(data);
	xassert(size);
	*data = NULL;
	*size = 0;

	fd = open(file_name, 0);
	if (fd < 0) {
		error("Error opening file %s, %m", file_name);
		return;
	}

	amount = read(fd, &rec_cnt, sizeof(uint16_t));
	if (amount < sizeof(uint16_t)) {
		if (amount != 0)	/* incomplete write */
			error("Error reading file %s, %m", file_name);
		else 
			verbose("File %s has zero size", file_name); 
		close(fd);
		return;
	}

	pos = 0;
	buf_size = 4096;
	buffer = xmalloc(buf_size);
	while (1) {
		amount = read(fd, &buffer[pos], buf_size);
		if (amount < 0) {
			error("Error reading file %s, %m", file_name);
			xfree(buffer);
			close(fd);
			return;
		}
		if (amount < buf_size)	/* end of file */
			break;
		pos += amount;
		xrealloc(buffer, (pos + buf_size));
	}
	close(fd);

	/* We have all the data, now let's compute the pointers */
	pos = 0;
	array_ptr = xmalloc(rec_cnt * sizeof(char *));
	for (i = 0; i < rec_cnt; i++) {
		array_ptr[i] = &buffer[pos];
		pos += strlen(&buffer[pos]) + 1;
		if ((pos > buf_size) && ((i + 1) < rec_cnt)) {
			error("Bad environment file %s", file_name);
			break;
		}
	}

	*size = rec_cnt;
	*data = array_ptr;
	return;
}

/*
 * Read a string from a file
 * IN file_name - file to read from
 * OUT data - pointer to  string 
 *	must be xfreed when no longer needed
 */
void _read_data_from_file(char *file_name, char **data)
{
	int fd, pos, buf_size, amount;
	char *buffer;

	xassert(file_name);
	xassert(data);
	*data = NULL;

	fd = open(file_name, 0);
	if (fd < 0) {
		error("Error opening file %s, %m", file_name);
		return;
	}

	pos = 0;
	buf_size = 4096;
	buffer = xmalloc(buf_size);
	while (1) {
		amount = read(fd, &buffer[pos], buf_size);
		if (amount < 0) {
			error("Error reading file %s, %m", file_name);
			xfree(buffer);
			close(fd);
			return;
		}
		if (amount < buf_size)	/* end of file */
			break;
		pos += amount;
		xrealloc(buffer, (pos + buf_size));
	}

	*data = buffer;
	close(fd);
	return;
}

/* _copy_job_desc_to_job_record - copy the job descriptor from the RPC  
 *	structure into the actual slurmctld job record */
static int
_copy_job_desc_to_job_record(job_desc_msg_t * job_desc,
			     struct job_record **job_rec_ptr,
			     struct part_record *part_ptr,
			     bitstr_t ** req_bitmap,
			     bitstr_t ** exc_bitmap)
{
	int error_code;
	struct job_details *detail_ptr;
	struct job_record *job_ptr;

	job_ptr = create_job_record(&error_code);
	if (error_code)
		return error_code;

	strncpy(job_ptr->partition, part_ptr->name, MAX_NAME_LEN);
	job_ptr->part_ptr = part_ptr;
	if (job_desc->job_id != NO_VAL)		/* already confirmed unique */
		job_ptr->job_id = job_desc->job_id;
	else
		_set_job_id(job_ptr);
	_add_job_hash(job_ptr);

	if (job_desc->name) {
		strncpy(job_ptr->name, job_desc->name,
			sizeof(job_ptr->name));
	}
	job_ptr->user_id    = (uid_t) job_desc->user_id;
	job_ptr->job_state  = JOB_PENDING;
	job_ptr->time_limit = job_desc->time_limit;
	job_ptr->alloc_sid  = job_desc->alloc_sid;
	job_ptr->alloc_node = xstrdup(job_desc->alloc_node);

	if (job_desc->priority != NO_VAL) /* already confirmed submit_uid==0 */
		job_ptr->priority = job_desc->priority;
	else
		_set_job_prio(job_ptr);

	if (job_desc->kill_on_node_fail != (uint16_t) NO_VAL)
		job_ptr->kill_on_node_fail = job_desc->kill_on_node_fail;

	detail_ptr = job_ptr->details;
	detail_ptr->num_procs = job_desc->num_procs;
	detail_ptr->min_nodes = job_desc->min_nodes;
	detail_ptr->max_nodes = job_desc->max_nodes;
	if (job_desc->req_nodes) {
		detail_ptr->req_nodes = _copy_nodelist_no_dup(job_desc->req_nodes);
		detail_ptr->req_node_bitmap = *req_bitmap;
		*req_bitmap = NULL;	/* Reused nothing left to free */
	}
	if (job_desc->exc_nodes) {
		detail_ptr->exc_nodes = _copy_nodelist_no_dup(job_desc->exc_nodes);
		detail_ptr->exc_node_bitmap = *exc_bitmap;
		*exc_bitmap = NULL;	/* Reused nothing left to free */
	}
	if (job_desc->features)
		detail_ptr->features = xstrdup(job_desc->features);
	if (job_desc->shared != (uint16_t) NO_VAL)
		detail_ptr->shared = job_desc->shared;
	if (job_desc->contiguous != (uint16_t) NO_VAL)
		detail_ptr->contiguous = job_desc->contiguous;
	if (job_desc->min_procs != NO_VAL)
		detail_ptr->min_procs = job_desc->min_procs;
	if (job_desc->min_memory != NO_VAL)
		detail_ptr->min_memory = job_desc->min_memory;
	if (job_desc->min_tmp_disk != NO_VAL)
		detail_ptr->min_tmp_disk = job_desc->min_tmp_disk;
	if (job_desc->err)
		detail_ptr->err = xstrdup(job_desc->err);
	if (job_desc->in)
		detail_ptr->in = xstrdup(job_desc->in);
	if (job_desc->out)
		detail_ptr->out = xstrdup(job_desc->out);
	if (job_desc->work_dir)
		detail_ptr->work_dir = xstrdup(job_desc->work_dir);

	*job_rec_ptr = job_ptr;
	return SLURM_SUCCESS;
}

/*
 * _copy_nodelist_no_dup - Take a node_list string and convert it to an 
 *	expression without duplicate names. For example, we want to convert 
 *	a users request for nodes "lx1,lx2,lx1,lx3" to "lx[1-3]"
 * node_list IN - string describing a list of nodes
 * RET a compact node expression, must be xfreed by the user
 */
static char *_copy_nodelist_no_dup(char *node_list)
{
	int   new_size = 64;
	char *new_str;
	hostlist_t hl = hostlist_create(node_list);
	if (hl == NULL)
		return NULL;

	hostlist_uniq(hl);
	new_str = xmalloc(new_size);
	while (hostlist_ranged_string(hl, new_size, new_str) == -1) {
		new_size *= 2;
		xrealloc(new_str, new_size);
	}
	hostlist_destroy(hl);
	return new_str;
}

/* 
 * job_time_limit - terminate jobs which have exceeded their time limit
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
void job_time_limit(void)
{
	ListIterator job_record_iterator;
	struct job_record *job_ptr;
	time_t now;

	now = time(NULL);
	job_record_iterator = list_iterator_create(job_list);
	while ((job_ptr =
		(struct job_record *) list_next(job_record_iterator))) {
		bool inactive_flag = false;
		xassert (job_ptr->magic == JOB_MAGIC);
		if (job_ptr->job_state != JOB_RUNNING)
			continue;

		if (slurmctld_conf.inactive_limit) {
			if (job_ptr->step_list &&
			    (list_count(job_ptr->step_list) > 0))
				job_ptr->time_last_active = now;
			else if ((job_ptr->time_last_active +
				  slurmctld_conf.inactive_limit) <= now) {
				/* job inactive, kill it */
				job_ptr->end_time   = now;
				job_ptr->time_limit = 1;
				inactive_flag       = true;
			}
		}
		if ((job_ptr->time_limit == INFINITE) ||
		    (job_ptr->end_time > now))
			continue;

		last_job_update = now;
		if (inactive_flag)
			info("Inactivity time limit reached for JobId=%u",
			     job_ptr->job_id);
		else
			info("Time limit exhausted for JobId=%u",
			     job_ptr->job_id);
		_job_timed_out(job_ptr);
	}

	list_iterator_destroy(job_record_iterator);
}

/* Terminate a job that has exhausted its time limit */
static void _job_timed_out(struct job_record *job_ptr)
{
	xassert(job_ptr);

	if (job_ptr->details) {
		time_t now      = time(NULL);
		job_ptr->end_time           = now;
		job_ptr->time_last_active   = now;
		job_ptr->job_state          = JOB_TIMEOUT | JOB_COMPLETING;
		deallocate_nodes(job_ptr, true);
	} else
		job_signal(job_ptr->job_id, SIGKILL, 0);
	return;
}

/* _validate_job_desc - validate that a job descriptor for job submit or 
 *	allocate has valid data, set values to defaults as required 
 * IN/OUT job_desc_msg - pointer to job descriptor, modified as needed
 * IN allocate - if clear job to be queued, if set allocate for user now 
 * IN submit_uid - who request originated
 */
static int _validate_job_desc(job_desc_msg_t * job_desc_msg, int allocate, 
			      uid_t submit_uid)
{
	if ((job_desc_msg->num_procs == NO_VAL) &&
	    (job_desc_msg->min_nodes == NO_VAL) &&
	    (job_desc_msg->req_nodes == NULL)) {
		info("Job failed to specify num_procs, min_nodes or req_nodes");
		return ESLURM_JOB_MISSING_SIZE_SPECIFICATION;
	}
	if ((allocate == SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0) &&
	    (job_desc_msg->script == NULL)) {
		info("_validate_job_desc: job failed to specify Script");
		return ESLURM_JOB_SCRIPT_MISSING;
	}
	if (job_desc_msg->user_id == NO_VAL) {
		info("_validate_job_desc: job failed to specify User");
		return ESLURM_USER_ID_MISSING;
	}
	if ((job_desc_msg->name) &&
	    (strlen(job_desc_msg->name) >= MAX_NAME_LEN)) {
		job_desc_msg->name[MAX_NAME_LEN-1] = '\0';
	}
	if (job_desc_msg->contiguous == (uint16_t) NO_VAL)
		job_desc_msg->contiguous = 0;
	if (job_desc_msg->kill_on_node_fail == (uint16_t) NO_VAL)
		job_desc_msg->kill_on_node_fail = 1;
	if (job_desc_msg->shared == (uint16_t) NO_VAL)
		job_desc_msg->shared = 0;

	if (job_desc_msg->job_id != NO_VAL) {
		struct job_record *dup_job_ptr;
		if ((submit_uid != 0) && 
		    (submit_uid != slurmctld_conf.slurm_user_id)) {
			info("attempt by uid %u to set job_id", submit_uid);
			return ESLURM_DUPLICATE_JOB_ID;
		}
		dup_job_ptr = find_job_record((uint32_t) job_desc_msg->job_id);
		if (dup_job_ptr && 
		    (!(IS_JOB_FINISHED(dup_job_ptr)))) {
			info("attempt re-use active job_id %u", 
			     job_desc_msg->job_id);
			return ESLURM_DUPLICATE_JOB_ID;
		}
		if (dup_job_ptr)	/* Purge the record for re-use */
			_purge_job_record(job_desc_msg->job_id);
	}

	if ((submit_uid != 0) &&	/* only root can set job priority */
	    (job_desc_msg->priority != 0))
		job_desc_msg->priority = NO_VAL;

	if (job_desc_msg->num_procs == NO_VAL)
		job_desc_msg->num_procs = 1;	/* default cpu count of 1 */
	if (job_desc_msg->min_nodes == NO_VAL)
		job_desc_msg->min_nodes = 1;	/* default node count of 1 */
	if (job_desc_msg->min_memory == NO_VAL)
		job_desc_msg->min_memory = 1;	/* default 1MB mem per node */
	if (job_desc_msg->min_tmp_disk == NO_VAL)
		job_desc_msg->min_tmp_disk = 1;	/* default 1MB disk per node */
	if (job_desc_msg->shared == (uint16_t) NO_VAL)
		job_desc_msg->shared = 0;	/* default not shared nodes */
	if (job_desc_msg->min_procs == NO_VAL)
		job_desc_msg->min_procs = 1;	/* default 1 cpu per node */
	return SLURM_SUCCESS;
}

/* 
 * _list_delete_job - delete a job record and its corresponding job_details,
 *	see common/list.h for documentation
 * IN job_entry - pointer to job_record to delete
 * global: job_list - pointer to global job list
 *	job_count - count of job list entries
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */
static void _list_delete_job(void *job_entry)
{
	struct job_record *job_ptr;
	int i, j;

	xassert(job_entry);
	job_ptr = (struct job_record *) job_entry;
	xassert (job_ptr->magic == JOB_MAGIC);

	if (job_hash[JOB_HASH_INX(job_ptr->job_id)] == job_ptr)
		job_hash[JOB_HASH_INX(job_ptr->job_id)] = NULL;
	else {
		for (i = 0; i < max_hash_over; i++) {
			if (job_hash_over[i] != job_ptr)
				continue;
			for (j = i + 1; j < max_hash_over; j++) {
				job_hash_over[j - 1] = job_hash_over[j];
			}
			job_hash_over[--max_hash_over] = NULL;
			break;
		}
	}

	delete_job_details(job_ptr);
	xfree(job_ptr->alloc_node);
	xfree(job_ptr->nodes);
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	xfree(job_ptr->cpus_per_node);
	xfree(job_ptr->cpu_count_reps);
	xfree(job_ptr->node_addr);
	if (job_ptr->step_list) {
		delete_all_step_records(job_ptr);
		list_destroy(job_ptr->step_list);
	}
	job_count--;
	xfree(job_ptr);
}


/*
 * _list_find_job_id - find specific job_id entry in the job list,  
 *	see common/list.h for documentation, key is job_id_ptr 
 * global- job_list - the global partition list
 */
static int _list_find_job_id(void *job_entry, void *key)
{
	uint32_t *job_id_ptr = (uint32_t *) key;

	if (((struct job_record *) job_entry)->job_id == *job_id_ptr)
		return 1;
	else
		return 0;
}


/*
 * _list_find_job_old - find old entries in the job list,  
 *	see common/list.h for documentation, key is ignored 
 * global- job_list - the global partition list
 */
static int _list_find_job_old(void *job_entry, void *key)
{
	time_t min_age = time(NULL) - slurmctld_conf.min_job_age;
	struct job_record *job_ptr = (struct job_record *)job_entry;

	if (slurmctld_conf.min_job_age == 0)
		return 0;	/* No job record purging */

	if (job_ptr->end_time > min_age)
		return 0;	/* Too new to purge */

	if (!(IS_JOB_FINISHED(job_ptr))) 
		return 0;	/* Job still active */

	if (job_ptr->job_state & JOB_COMPLETING) {
		re_kill_job(job_ptr);
		return 0;	/* Job still completing */
	}

	return 1;		/* Purge the job */
}


/* 
 * pack_all_jobs - dump all job information for all jobs in 
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c 
 *	whenever the data format changes
 */
void
pack_all_jobs(char **buffer_ptr, int *buffer_size)
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	uint32_t jobs_packed = 0, tmp_offset;
	Buf buffer;
	time_t now = time(NULL);

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE * 16);

	/* write message body header : size and time */
	/* put in a place holder job record count of 0 for now */
	pack32((uint32_t) jobs_packed, buffer);
	pack_time(now, buffer);

	/* write individual job records */
	job_record_iterator = list_iterator_create(job_list);
	while ((job_record_point =
		(struct job_record *) list_next(job_record_iterator))) {
		xassert (job_record_point->magic == JOB_MAGIC);

		pack_job(job_record_point, buffer);
		jobs_packed++;
	}

	list_iterator_destroy(job_record_iterator);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32((uint32_t) jobs_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}


/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission)
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * NOTE: change _unpack_job_info_members() in common/slurm_protocol_pack.c
 *	  whenever the data format changes
 */
void pack_job(struct job_record *dump_job_ptr, Buf buffer)
{
	struct job_details *detail_ptr;

	pack32(dump_job_ptr->job_id, buffer);
	pack32(dump_job_ptr->user_id, buffer);

	pack16((uint16_t) dump_job_ptr->job_state, buffer);
	pack16((uint16_t) dump_job_ptr->batch_flag, buffer);
	pack32(dump_job_ptr->alloc_sid, buffer);
	pack32(dump_job_ptr->time_limit, buffer);

	pack_time(dump_job_ptr->start_time, buffer);
	pack_time(dump_job_ptr->end_time, buffer);
	pack32(dump_job_ptr->priority, buffer);

	packstr(dump_job_ptr->nodes, buffer);
	packstr(dump_job_ptr->partition, buffer);
	packstr(dump_job_ptr->name, buffer);
	packstr(dump_job_ptr->alloc_node, buffer);
	pack_bit_fmt(dump_job_ptr->node_bitmap, buffer);

	detail_ptr = dump_job_ptr->details;
	if (detail_ptr && dump_job_ptr->job_state == JOB_PENDING)
		_pack_job_details(detail_ptr, buffer);
	else
		_pack_job_details(NULL, buffer);
}

/* pack job details for "get_job_info" RPC */
static void _pack_job_details(struct job_details *detail_ptr, Buf buffer)
{
	if (detail_ptr) {
		pack32((uint32_t) detail_ptr->num_procs, buffer);
		pack32((uint32_t) detail_ptr->min_nodes, buffer);
		pack16((uint16_t) detail_ptr->shared, buffer);
		pack16((uint16_t) detail_ptr->contiguous, buffer);

		pack32((uint32_t) detail_ptr->min_procs, buffer);
		pack32((uint32_t) detail_ptr->min_memory, buffer);
		pack32((uint32_t) detail_ptr->min_tmp_disk, buffer);

		packstr(detail_ptr->req_nodes, buffer);
		pack_bit_fmt(detail_ptr->req_node_bitmap, buffer);
		packstr(detail_ptr->features, buffer);
	} 

	else {
		pack32((uint32_t) 0, buffer);
		pack32((uint32_t) 0, buffer);
		pack16((uint16_t) 0, buffer);
		pack16((uint16_t) 0, buffer);

		pack32((uint32_t) 0, buffer);
		pack32((uint32_t) 0, buffer);
		pack32((uint32_t) 0, buffer);

		packstr(NULL, buffer);
		packstr(NULL, buffer);
		packstr(NULL, buffer);
	}
}

/*
 * purge_old_job - purge old job records. 
 *	the jobs must have completed at least MIN_JOB_AGE minutes ago
 * global: job_list - global job table
 *	last_job_update - time of last job table update
 */
void purge_old_job(void)
{
	int i;

	i = list_delete_all(job_list, &_list_find_job_old, "");
	if (i) {
		debug2("purge_old_job: purged %d old job records", i);
		last_job_update = time(NULL);
	}
}


/*
 * _purge_job_record - purge specific job record
 * IN job_id - job_id of job record to be purged
 * RET int - count of job's purged
 * global: job_list - global job table
 */
static int _purge_job_record(uint32_t job_id)
{
	return list_delete_all(job_list, &_list_find_job_id, (void *) &job_id);
}


/* 
 * reset_job_bitmaps - reestablish bitmaps for existing jobs. 
 *	this should be called after rebuilding node information, 
 *	but before using any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
void reset_job_bitmaps(void)
{
	ListIterator job_record_iterator;
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	bool job_fail = false;

	xassert(job_list);

	job_record_iterator = list_iterator_create(job_list);
	while ((job_ptr =
		(struct job_record *) list_next(job_record_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		job_fail = false;
		part_ptr = list_find_first(part_list, &list_find_part,
					   job_ptr->partition);
		if (part_ptr == NULL) {
			error("Invalid partition (%s) for job_id %u", 
		    	      job_ptr->partition, job_ptr->job_id);
			job_fail = true;
		}
		job_ptr->part_ptr = part_ptr;

		FREE_NULL_BITMAP(job_ptr->node_bitmap);
		if ((job_ptr->nodes) && 
		    (node_name2bitmap(job_ptr->nodes, &job_ptr->node_bitmap))) {
			error("Invalid nodes (%s) for job_id %u", 
		    	      job_ptr->nodes, job_ptr->job_id);
			job_fail = true;
		}
		build_node_details(job_ptr);	/* set: num_cpu_groups, 
						 * cpu_count_reps, node_cnt, 
						 * cpus_per_node, node_addr */
		if (_reset_detail_bitmaps(job_ptr))
			job_fail = true;

		_reset_step_bitmaps(job_ptr);

		if ((job_ptr->kill_on_step_done) &&
		    (list_count(job_ptr->step_list) <= 1))
			job_fail = true;

		if (job_fail) {
			if (job_ptr->job_state == JOB_PENDING) {
				job_ptr->start_time = 
					job_ptr->end_time = time(NULL);
				job_ptr->job_state = JOB_NODE_FAIL;
			} else if (job_ptr->job_state == JOB_RUNNING) {
				job_ptr->end_time = time(NULL);
				job_ptr->job_state = JOB_NODE_FAIL | 
						     JOB_COMPLETING;
			}
			delete_all_step_records(job_ptr);
		}
	}

	list_iterator_destroy(job_record_iterator);
	last_job_update = time(NULL);
}

static int _reset_detail_bitmaps(struct job_record *job_ptr)
{
	if (job_ptr->details == NULL) 
		return SLURM_SUCCESS;

	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
	if ((job_ptr->details->req_nodes) && 
	    (node_name2bitmap(job_ptr->details->req_nodes, 
			      &job_ptr->details->req_node_bitmap))) {
		error("Invalid req_nodes (%s) for job_id %u", 
	    	      job_ptr->details->req_nodes, job_ptr->job_id);
		return SLURM_ERROR;
	}

	FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
	if ((job_ptr->details->exc_nodes) && 
	    (node_name2bitmap(job_ptr->details->exc_nodes, 
			      &job_ptr->details->exc_node_bitmap))) {
		error("Invalid exc_nodes (%s) for job_id %u", 
	    	      job_ptr->details->exc_nodes, job_ptr->job_id);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void _reset_step_bitmaps(struct job_record *job_ptr)
{
	ListIterator step_record_iterator;
	struct step_record *step_ptr;

	step_record_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) 
			   list_next (step_record_iterator))) {
		if ((step_ptr->step_node_list) && 		
		    (node_name2bitmap(step_ptr->step_node_list, 
			      &step_ptr->step_node_bitmap))) {
			error("Invalid step_node_list (%s) for step_id %u.%u", 
	   	 	      step_ptr->step_node_list, 
			      job_ptr->job_id, step_ptr->step_id);
			delete_step_record (job_ptr, step_ptr->step_id);
		}
	}		

	list_iterator_destroy (step_record_iterator);
	return;
}

/* update first assigned job id as needed on reconfigure */
void reset_first_job_id(void)
{
	if (job_id_sequence < slurmctld_conf.first_job_id)
		job_id_sequence = slurmctld_conf.first_job_id;
}

/*
 * _set_job_id - set a default job_id, insure that it is unique
 * IN job_ptr - pointer to the job_record
 */
static void _set_job_id(struct job_record *job_ptr)
{
	uint32_t new_id;

	if (job_id_sequence < 0)
		job_id_sequence = slurmctld_conf.first_job_id;

	xassert(job_ptr);
	xassert (job_ptr->magic == JOB_MAGIC);
	if ((job_ptr->partition == NULL)
	    || (strlen(job_ptr->partition) == 0))
		fatal("_set_job_id: partition not set");

	/* Insure no conflict in job id if we roll over 32 bits */
	while (1) {
		if (++job_id_sequence >= MIN_NOALLOC_JOBID)
			job_id_sequence = slurmctld_conf.first_job_id;
		new_id = job_id_sequence;
		if (find_job_record(new_id) == NULL) {
			job_ptr->job_id = new_id;
			break;
		}
	}
}


/*
 * _set_job_prio - set a default job priority
 * IN job_ptr - pointer to the job_record
 * NOTE: this is a simple prototype, we need to re-establish value on restart
 */
static void _set_job_prio(struct job_record *job_ptr)
{
	xassert(job_ptr);
	xassert (job_ptr->magic == JOB_MAGIC);
	job_ptr->priority = default_prio--;
}


/* After a node is returned to service, reset the priority of jobs 
 * which may have been held due to that node being unavailable */
void reset_job_priority(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	int count = 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->priority == 1) {
			_set_job_prio(job_ptr);
			count++;
		}
	}
	list_iterator_destroy(job_iterator);
	if (count)
		last_job_update = time(NULL);
}

/* 
 * _top_priority - determine if any other job for this partition has a 
 *	higher priority than specified job
 * IN job_ptr - pointer to selected partition
 * RET true if selected job has highest priority
 */
static bool _top_priority(struct job_record *job_ptr)
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	bool top;

	if (job_ptr->priority == 0)	/* held */
		return false;

	top = true;		/* assume top priority until found otherwise */
	job_record_iterator = list_iterator_create(job_list);
	while ((job_record_point =
		(struct job_record *) list_next(job_record_iterator))) {
		xassert (job_record_point->magic == JOB_MAGIC);
		if (job_record_point == job_ptr)
			continue;
		if (job_record_point->job_state != JOB_PENDING)
			continue;
		if ((job_record_point->priority > job_ptr->priority) &&
		    (job_record_point->part_ptr == job_ptr->part_ptr)) {
			top = false;
			break;
		}
	}

	list_iterator_destroy(job_record_iterator);
	return top;
}


/*
 * update_job - update a job's parameters per the supplied specifications
 * IN job_specs - a job's specification
 * IN uid - uid of user issuing RPC
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
int update_job(job_desc_msg_t * job_specs, uid_t uid)
{
	int error_code = SLURM_SUCCESS;
	int super_user = 0;
	struct job_record *job_ptr;
	struct job_details *detail_ptr;
	struct part_record *tmp_part_ptr;
	bitstr_t *req_bitmap = NULL;
	time_t now = time(NULL);

	job_ptr = find_job_record(job_specs->job_id);
	if (job_ptr == NULL) {
		error("update_job: job_id %u does not exist.",
		      job_specs->job_id);
		return ESLURM_INVALID_JOB_ID;
	}
	if ((uid == 0) || (uid == getuid()))
		super_user = 1;
	if ((job_ptr->user_id != uid) && (super_user == 0)) {
		error("Security violation, JOB_UPDATE RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	detail_ptr = job_ptr->details;
	last_job_update = now;

	if ((job_specs->time_limit != NO_VAL) && (!IS_JOB_FINISHED(job_ptr))) {
		if (super_user ||
		    (job_ptr->time_limit > job_specs->time_limit)) {
			job_ptr->time_limit = job_specs->time_limit;
			if (job_ptr->time_limit == INFINITE)	/* one year */
				job_ptr->end_time = job_ptr->start_time +
						    (365 * 24 * 60 * 60);
			else
				job_ptr->end_time = job_ptr->start_time +
						    (job_ptr->time_limit * 60);
			if (job_ptr->end_time < now)
				job_ptr->end_time = now;
			if ((job_ptr->job_state == JOB_RUNNING) &&
			    (list_is_empty(job_ptr->step_list) == 0))
				_xmit_new_end_time(job_ptr);
			info("update_job: setting time_limit to %u for job_id %u", 
			     job_specs->time_limit, job_specs->job_id);
		} else {
			error("Attempt to increase time limit for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->priority != NO_VAL) {
		if (super_user ||
		    (job_ptr->priority > job_specs->priority)) {
			job_ptr->priority = job_specs->priority;
			info("update_job: setting priority to %u for job_id %u", 
			     job_specs->priority, job_specs->job_id);
		} else {
			error("Attempt to increase priority for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->min_procs != NO_VAL && detail_ptr) {
		if (super_user ||
		    (detail_ptr->min_procs > job_specs->min_procs)) {
			detail_ptr->min_procs = job_specs->min_procs;
			info("update_job: setting min_procs to %u for job_id %u", 
			     job_specs->min_procs, job_specs->job_id);
		} else {
			error("Attempt to increase min_procs for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->min_memory != NO_VAL && detail_ptr) {
		if (super_user ||
		    (detail_ptr->min_memory > job_specs->min_memory)) {
			detail_ptr->min_memory = job_specs->min_memory;
			info("update_job: setting min_memory to %u for job_id %u", 
			     job_specs->min_memory, job_specs->job_id);
		} else {
			error("Attempt to increase min_memory for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->min_tmp_disk != NO_VAL && detail_ptr) {
		if (super_user ||
		    (detail_ptr->min_tmp_disk > job_specs->min_tmp_disk)) {
			detail_ptr->min_tmp_disk = job_specs->min_tmp_disk;
			info("update_job: setting min_tmp_disk to %u for job_id %u", 
			     job_specs->min_tmp_disk, job_specs->job_id);
		} else {
			error
			    ("Attempt to increase min_tmp_disk for job %u",
			     job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->num_procs != NO_VAL && detail_ptr) {
		if (super_user ||
		    (detail_ptr->num_procs > job_specs->num_procs)) {
			detail_ptr->num_procs = job_specs->num_procs;
			info("update_job: setting num_procs to %u for job_id %u", 
			     job_specs->num_procs, job_specs->job_id);
		} else {
			error("Attempt to increase num_procs for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->min_nodes != NO_VAL && detail_ptr) {
		if (super_user ||
		    (detail_ptr->min_nodes > job_specs->min_nodes)) {
			detail_ptr->min_nodes = job_specs->min_nodes;
			info("update_job: setting min_nodes to %u for job_id %u", 
			     job_specs->min_nodes, job_specs->job_id);
		} else {
			error("Attempt to increase min_nodes for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->shared != (uint16_t) NO_VAL && detail_ptr) {
		if (super_user || (detail_ptr->shared > job_specs->shared)) {
			detail_ptr->shared = job_specs->shared;
			info("update_job: setting shared to %u for job_id %u", 
			     job_specs->shared, job_specs->job_id);
		} else {
			error("Attempt to remove sharing for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->contiguous != (uint16_t) NO_VAL && detail_ptr) {
		if (super_user ||
		    (detail_ptr->contiguous > job_specs->contiguous)) {
			detail_ptr->contiguous = job_specs->contiguous;
			info("update_job: setting contiguous to %u for job_id %u", 
			     job_specs->contiguous, job_specs->job_id);
		} else {
			error("Attempt to add contiguous for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->kill_on_node_fail != (uint16_t) NO_VAL) {
		job_ptr->kill_on_node_fail = job_specs->kill_on_node_fail;
		info("update_job: setting kill_on_node_fail to %u for job_id %u", 
		     job_specs->kill_on_node_fail, job_specs->job_id);
	}

	if (job_specs->features && detail_ptr) {
		if (super_user) {
			xfree(detail_ptr->features);
			detail_ptr->features = job_specs->features;
			info("update_job: setting features to %s for job_id %u", 
			     job_specs->features, job_specs->job_id);
			job_specs->features = NULL;
		} else {
			error("Attempt to change features for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->name) {
		strncpy(job_ptr->name, job_specs->name, MAX_NAME_LEN);
		info("update_job: setting name to %s for job_id %u",
		     job_specs->name, job_specs->job_id);
	}

	if (job_specs->partition) {
		tmp_part_ptr = find_part_record(job_specs->partition);
		if (tmp_part_ptr == NULL)
			error_code = ESLURM_INVALID_PARTITION_NAME;
		if ((super_user && tmp_part_ptr)) {
			strncpy(job_ptr->partition, job_specs->partition,
				MAX_NAME_LEN);
			job_ptr->part_ptr = tmp_part_ptr;
			info("update_job: setting partition to %s for job_id %u", 
			     job_specs->partition, job_specs->job_id);
			job_specs->partition = NULL;
		} else {
			error("Attempt to change partition for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs->req_nodes && detail_ptr) {
		if (super_user) {
			if (node_name2bitmap
			    (job_specs->req_nodes, &req_bitmap)) {
				error
				    ("Invalid node list specified for job_update: %s",
				     job_specs->req_nodes);
				FREE_NULL_BITMAP(req_bitmap);
				req_bitmap = NULL;
				error_code = ESLURM_INVALID_NODE_NAME;
			}
			if (req_bitmap) {
				xfree(detail_ptr->req_nodes);
				detail_ptr->req_nodes =
				    job_specs->req_nodes;
				FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
				detail_ptr->req_node_bitmap = req_bitmap;
				info("update_job: setting req_nodes to %s for job_id %u", 
				     job_specs->req_nodes, job_specs->job_id);
				job_specs->req_nodes = NULL;
			}
		} else {
			error("Attempt to change req_nodes for job %u",
			      job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	return error_code;
}


/*
 * validate_jobs_on_node - validate that any jobs that should be on the node 
 *	are actually running, if not clean up the job records and/or node 
 *	records
 * IN node_name - node which should have jobs running
 * IN/OUT job_count - number of jobs which should be running on specified node
 * IN job_id_ptr - pointer to array of job_ids that should be on this node
 * IN step_id_ptr - pointer to array of job step ids that should be on node
 */
void
validate_jobs_on_node(char *node_name, uint32_t * job_count,
		      uint32_t * job_id_ptr, uint16_t * step_id_ptr)
{
	int i, node_inx, jobs_on_node;
	struct node_record *node_ptr;
	struct job_record *job_ptr;
	time_t now = time(NULL);

	node_ptr = find_node_record(node_name);
	if (node_ptr == NULL) {
		error("slurmd registered on unknown node %s", node_name);
		return;
	}
	node_inx = node_ptr - node_record_table_ptr;

	/* Check that jobs running are really supposed to be there */
	for (i = 0; i < *job_count; i++) {
		if ( (job_id_ptr[i] >= MIN_NOALLOC_JOBID) && 
		     (job_id_ptr[i] <= MAX_NOALLOC_JOBID) ) {
			info("NoAllocate job %u.%u reported on node %s",
				job_id_ptr[i], step_id_ptr[i], node_name);
			continue;
		}

		job_ptr = find_job_record(job_id_ptr[i]);
		if (job_ptr == NULL) {
			error("Orphan job %u.%u reported on node %s",
			      job_id_ptr[i], step_id_ptr[i], node_name);
			_kill_job_on_node(job_id_ptr[i], node_ptr);
		}

		else if (job_ptr->job_state == JOB_RUNNING) {
			if (bit_test(job_ptr->node_bitmap, node_inx)) {
				debug3("Registered job %u.%u on node %s ",
				       job_id_ptr[i], step_id_ptr[i], 
				       node_name);
				if ((job_ptr->batch_flag) &&
				    (node_inx == bit_ffs(job_ptr->node_bitmap)))
					job_ptr->time_last_active = now;
			} else {
				error
				    ("Registered job %u.u on wrong node %s ",
				     job_id_ptr[i], step_id_ptr[i], node_name);
				_kill_job_on_node(job_id_ptr[i], node_ptr);
			}
		}

		else if (job_ptr->job_state & JOB_COMPLETING) {
			/* Re-send kill request as needed, not necessarily an error */
			_kill_job_on_node(job_id_ptr[i], node_ptr);
		}


		else if (job_ptr->job_state == JOB_PENDING) {
			error("Registered PENDING job %u.%u on node %s ",
			      job_id_ptr[i], step_id_ptr[i], node_name);
			/* FIXME: Could possibly recover the job */
			job_ptr->job_state = JOB_FAILED;
			last_job_update = time(NULL);
			job_ptr->end_time = time(NULL);
			delete_job_details(job_ptr);
			_kill_job_on_node(job_id_ptr[i], node_ptr);
		}

		else {		/* else job is supposed to be done */
			error
			    ("Registered job %u.%u in state %s on node %s ",
			     job_id_ptr[i], step_id_ptr[i], 
			     job_state_string(job_ptr->job_state),
			     node_name);
			_kill_job_on_node(job_id_ptr[i], node_ptr);
		}
	}

	jobs_on_node = node_ptr->run_job_cnt + node_ptr->comp_job_cnt;
	if (jobs_on_node)
		_purge_lost_batch_jobs(node_inx, now);

	if (jobs_on_node != *job_count) {
		/* slurmd will not know of a job unless the job has
		 * steps active at registration time, so this is not 
		 * an error condition, slurmd is also reporting steps 
		 * rather than jobs */
		debug3("resetting job_count on node %s from %d to %d", 
		     node_name, *job_count, jobs_on_node);
		*job_count = jobs_on_node;
	}

	return;
}

/* Purge any batch job that should have its script running on node 
 * node_inx, but is not (i.e. its time_last_active != now) */
static void _purge_lost_batch_jobs(int node_inx, time_t now)
{
	ListIterator job_record_iterator;
	struct job_record *job_ptr;

	job_record_iterator = list_iterator_create(job_list);
	while ((job_ptr =
		    (struct job_record *) list_next(job_record_iterator))) {
		if ((job_ptr->job_state != JOB_RUNNING) ||
		    (job_ptr->batch_flag == 0)          ||
		    (job_ptr->time_last_active == now)  ||
		    (node_inx != bit_ffs(job_ptr->node_bitmap)))
			continue;

		info("Master node lost JobId=%u, killing it", 
			job_ptr->job_id);
		job_complete(job_ptr->job_id, 0, false, 0);
	}
	list_iterator_destroy(job_record_iterator);
}

/*
 * _kill_job_on_node - Kill the specific job_id on a specific node,
 *	the request is not processed immediately, but queued. 
 *	This is to prevent a flood of pthreads if slurmctld restarts 
 *	without saved state and slurmd daemons register with a 
 *	multitude of running jobs. Slurmctld will not recognize 
 *	these jobs and use this function to kill them - one 
 *	agent request per node as they register.
 * IN job_id - id of the job to be killed
 * IN node_ptr - pointer to the node on which the job resides
 */
static void
_kill_job_on_node(uint32_t job_id, struct node_record *node_ptr)
{
	agent_arg_t *agent_info;
	kill_job_msg_t *kill_req;

	debug("Killing job %u on node %s", job_id, node_ptr->name);

	kill_req = xmalloc(sizeof(kill_job_msg_t));
	kill_req->job_id	= job_id;

	agent_info = xmalloc(sizeof(agent_arg_t));
	agent_info->node_count	= 1;
	agent_info->retry	= 0;
	agent_info->slurm_addr	= xmalloc(sizeof(slurm_addr));
	memcpy(agent_info->slurm_addr, 
	       &node_ptr->slurm_addr, sizeof(slurm_addr));
	agent_info->node_names	= xstrdup(node_ptr->name);
	agent_info->msg_type	= REQUEST_KILL_JOB;
	agent_info->msg_args	= kill_req;

	agent_queue_request(agent_info);
}


/*
 * old_job_info - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_id - ID of job for which info is requested
 * OUT everything else - the job's details
 */
int
old_job_info(uint32_t uid, uint32_t job_id, char **node_list,
	     uint16_t * num_cpu_groups, uint32_t ** cpus_per_node,
	     uint32_t ** cpu_count_reps, uint16_t * node_cnt,
	     slurm_addr ** node_addr)
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;
	if ((uid != 0) && (job_ptr->user_id != uid))
		return ESLURM_ACCESS_DENIED;
	if (IS_JOB_PENDING(job_ptr))
		return ESLURM_JOB_PENDING;
	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	if (node_list)
		*node_list = job_ptr->nodes;
	if (num_cpu_groups)
		*num_cpu_groups = job_ptr->num_cpu_groups;
	if (cpus_per_node)
		*cpus_per_node = job_ptr->cpus_per_node;
	if (cpu_count_reps)
		*cpu_count_reps = job_ptr->cpu_count_reps;
	if (node_cnt)
		*node_cnt = job_ptr->node_cnt;
	if (node_addr)
		*node_addr = job_ptr->node_addr;
	return SLURM_SUCCESS;
}

/*
 * Synchronize the batch job in the system with their files.
 * All pending batch jobs must have script and environment files
 * No other jobs should have such files
 */
int sync_job_files(void)
{
	List batch_dirs;

	batch_dirs = list_create(_del_batch_list_rec);
	_get_batch_job_dir_ids(batch_dirs);
	_validate_job_files(batch_dirs);
	_remove_defunct_batch_dirs(batch_dirs);
	list_destroy(batch_dirs);
	return SLURM_SUCCESS;
}

/* Append to the batch_dirs list the job_id's associated with 
 *	every batch job directory in existence */
static void _get_batch_job_dir_ids(List batch_dirs)
{
	DIR *f_dir;
	struct dirent *dir_ent;
	long long_job_id;
	uint32_t *job_id_ptr;
	char *endptr;

	f_dir = opendir(slurmctld_conf.state_save_location);
	if (!f_dir) {
		error("opendir(%s): %m", 
		      slurmctld_conf.state_save_location);
		return;
	}

	while ((dir_ent = readdir(f_dir))) {
		if (strncmp("job.#", dir_ent->d_name, 4))
			continue;
		long_job_id = strtol(&dir_ent->d_name[4], &endptr, 10);
		if ((long_job_id == 0) || (endptr[0] != '\0'))
			continue;
		debug3("found batch directory for job_id %ld",long_job_id);
		job_id_ptr = xmalloc(sizeof(uint32_t));
		*job_id_ptr = long_job_id;
		list_append (batch_dirs, job_id_ptr);
	}

	closedir(f_dir);
}

/* All pending batch jobs must have a batch_dir entry, 
 *	otherwise we flag it as FAILED and don't schedule
 * If the batch_dir entry exists for a PENDING or RUNNING batch job, 
 *	remove it the list (of directories to be deleted) */
static void _validate_job_files(List batch_dirs)
{
	ListIterator job_record_iterator;
	struct job_record *job_ptr;
	int del_cnt;

	job_record_iterator = list_iterator_create(job_list);
	while ((job_ptr =
		    (struct job_record *) list_next(job_record_iterator))) {
		if (!job_ptr->batch_flag)
			continue;
		if (IS_JOB_FINISHED(job_ptr))
			continue;
		/* Want to keep this job's files */
		del_cnt = list_delete_all(batch_dirs, _find_batch_dir, 
					  &(job_ptr->job_id));
		if ((del_cnt == 0) && 
		    (job_ptr->job_state == JOB_PENDING)) {
			error("Script for job %u lost, state set to FAILED",
			      job_ptr->job_id);
			job_ptr->job_state = JOB_FAILED;
			job_ptr->start_time = job_ptr->end_time = time(NULL);
		}
	}
	list_iterator_destroy(job_record_iterator);
}

/* List matching function, see common/list.h */
static int _find_batch_dir(void *x, void *key)
{
	uint32_t *key1 = x;
	uint32_t *key2 = key;
	return (int)(*key1 == *key2);
}
/* List entry deletion function, see common/list.h */
static void _del_batch_list_rec(void *x)
{
	xfree(x);
}

/* Remove all batch_dir entries in the list */
static void _remove_defunct_batch_dirs(List batch_dirs)
{
	ListIterator batch_dir_inx;
	uint32_t *job_id_ptr;

	batch_dir_inx = list_iterator_create(batch_dirs);
	while ((job_id_ptr = list_next(batch_dir_inx))) {
		error("Purging files for defunct batch job %u",
		      *job_id_ptr);
		_delete_job_desc_files(*job_id_ptr);
	}
	list_iterator_destroy(batch_dir_inx);
}

/*
 *  _xmit_new_end_time
 *	Tell all slurmd's associated with a job of its new end time
 * IN job_ptr - pointer to terminating job
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
static void 
_xmit_new_end_time(struct job_record *job_ptr)
{
	job_time_msg_t *job_time_msg_ptr;
	agent_arg_t *agent_args;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int buf_rec_size = 0, i, retries = 0;

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_UPDATE_JOB_TIME;
	agent_args->retry = 1;
	job_time_msg_ptr = xmalloc(sizeof(job_time_msg_t));
	job_time_msg_ptr->job_id          = job_ptr->job_id;
	job_time_msg_ptr->expiration_time = job_ptr->end_time;

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		if ((agent_args->node_count + 1) > buf_rec_size) {
			buf_rec_size += 32;
			xrealloc((agent_args->slurm_addr),
				 (sizeof(struct sockaddr_in) *
				  buf_rec_size));
			xrealloc((agent_args->node_names),
				 (MAX_NAME_LEN * buf_rec_size));
		}
		agent_args->slurm_addr[agent_args->node_count] =
		    node_record_table_ptr[i].slurm_addr;
		strncpy(&agent_args->
			node_names[MAX_NAME_LEN * agent_args->node_count],
			node_record_table_ptr[i].name, MAX_NAME_LEN);
		agent_args->node_count++;
	}

	agent_args->msg_args = job_time_msg_ptr;
	debug("Spawning job time limit update agent");
	if (pthread_attr_init(&attr_agent))
		fatal("pthread_attr_init error %m");
	if (pthread_attr_setdetachstate
	    (&attr_agent, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	if (pthread_attr_setscope(&attr_agent, PTHREAD_SCOPE_SYSTEM))
		error("pthread_attr_setscope error %m");
#endif
	while (pthread_create(&thread_agent, &attr_agent, agent, 
			(void *) agent_args)) {
		error("pthread_create error %m");
		if (++retries > MAX_RETRIES)
			fatal("Can't create pthread");
		sleep(1);	/* sleep and try again */
	}
	return;
}


/*
 * job_epilog_complete - Note the completion of the epilog script for a 
 *	given job
 * IN job_id      - id of the job for which the epilog was executed
 * IN node_name   - name of the node on which the epilog was executed
 * IN return_code - return code from epilog script
 * RET true if job is COMPLETED, otherwise false
 */
bool job_epilog_complete(uint32_t job_id, char *node_name, 
		uint32_t return_code)
{
	struct job_record  *job_ptr = find_job_record(job_id);

	if (job_ptr == NULL)
		return true;

	if (return_code) {
		set_node_down(node_name, "Epilog error");
	} else {
		struct node_record *node_ptr = find_node_record(node_name);
		if (node_ptr)
			make_node_idle(node_ptr, job_ptr);
	}

	if (!(job_ptr->job_state & JOB_COMPLETING))	/* COMPLETED */
		return true;
	else
		return false;
}

/* job_fini - free all memory associated with job records */
void job_fini (void) 
{
	if (job_list) {
		list_destroy(job_list);
		job_list = NULL;
	}
	xfree(job_hash);
	xfree(job_hash_over);
}

