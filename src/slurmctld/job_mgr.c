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
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_LIBELAN3
#  include <elan3/elan3.h>
#  include <elan3/elanvp.h>
#  define BUF_SIZE (1024 + QSW_PACK_SIZE)
#else
#  define BUF_SIZE 1024
#endif

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_errno.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include "src/common/credential_utils.h"
slurm_ssl_key_ctx_t sign_ctx;

#define DETAILS_FLAG 0xdddd
#define MAX_STR_PACK 128
#define SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 0
#define STEP_FLAG 0xbbbb
#define TOP_PRIORITY 0xffff0000	/* large, but leave headroom for higher */

#define FREE_NULL(_X)			\
	do {				\
		if (_X) xfree (_X);	\
		_X	= NULL; 	\
	} while (0)

#define FREE_NULL_BITMAP(_X)			\
	do {				\
		if (_X) bit_free (_X);	\
		_X	= NULL; 	\
	} while (0)

#define JOB_HASH_INX(_job_id)	(_job_id % MAX_JOB_COUNT)

#define YES_OR_NO(_in_string)	\
		(( strcmp ((_in_string),"YES"))? \
			(strcmp((_in_string),"NO")? \
				-1 : 0 ) : 1 )
/* Global variables */
List job_list = NULL;		/* job_record list */
time_t last_job_update;		/* time of last update to job records */

/* Local variables */
static int default_prio = TOP_PRIORITY;
static int job_count;		/* job's in the system */
static long job_id_sequence = -1;	/* first job_id to assign new job */
static struct job_record *job_hash[MAX_JOB_COUNT];
static struct job_record *job_hash_over[MAX_JOB_COUNT];
static int max_hash_over = 0;

/* Local functions */
static void _add_job_hash(struct job_record *job_ptr);
static int  _copy_job_desc_to_file(job_desc_msg_t * job_desc,
				   uint32_t job_id);
static int  _copy_job_desc_to_job_record(job_desc_msg_t * job_desc,
					 struct job_record **job_ptr,
					 struct part_record *part_ptr,
					 bitstr_t * req_bitmap);
static void _delete_job_desc_files(uint32_t job_id);
static void _dump_job_details_state(struct job_details *detail_ptr,
				    Buf buffer);
static void _dump_job_state(struct job_record *dump_job_ptr, Buf buffer);
static void _dump_job_step_state(struct step_record *step_ptr, Buf buffer);
static int  _job_create(job_desc_msg_t * job_specs, uint32_t * new_job_id,
		        int allocate, int will_run,
		        struct job_record **job_rec_ptr, uid_t submit_uid);
static void _list_delete_job(void *job_entry);
static int  _list_find_job_old(void *job_entry, void *key);
static void _read_data_array_from_file(char *file_name, char ***data,
				       uint16_t * size);
static void _read_data_from_file(char *file_name, char **data);
static void _set_job_id(struct job_record *job_ptr);
static void _set_job_prio(struct job_record *job_ptr);
static void _signal_job_on_node(uint32_t job_id, uint16_t step_id,
				int signum, char *node_name);
static int _top_priority(struct job_record *job_ptr);
static int _validate_job_desc(job_desc_msg_t * job_desc_msg, int allocate);
static int _validate_job_create_req(job_desc_msg_t * job_desc);
static int _write_data_to_file(char *file_name, char *data);
static int _write_data_array_to_file(char *file_name, char **data,
				     uint16_t size);

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

	if (job_count >= MAX_JOB_COUNT) {
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

	job_record_point->magic = JOB_MAGIC;
	job_record_point->details = job_details_point;
	job_record_point->step_list = list_create(NULL);
	if (job_record_point->step_list == NULL)
		fatal("list_create can not allocate memory");

	job_details_point->magic = DETAILS_MAGIC;
	job_details_point->submit_time = time(NULL);

	if (list_append(job_list, job_record_point) == NULL)
		fatal("create_job_record: unable to allocate memory");

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
	if (job_entry->details->magic != DETAILS_MAGIC)
		fatal
		    ("delete_job_details: passed invalid job details pointer");
	FREE_NULL(job_entry->details->req_nodes);
	FREE_NULL_BITMAP(job_entry->details->req_node_bitmap);
	FREE_NULL(job_entry->details->features);
	FREE_NULL(job_entry->details->err);
	FREE_NULL(job_entry->details->in);
	FREE_NULL(job_entry->details->out);
	FREE_NULL(job_entry->details->work_dir);
	xfree(job_entry->details);
	job_entry->details = NULL;
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

/* dump_all_job_state - save the state of all jobs to file
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
		if (job_record_point->magic != JOB_MAGIC)
			fatal("dump_all_job: job integrity is bad");
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

	/* Dump job details, if available */
	detail_ptr = dump_job_ptr->details;
	if (detail_ptr) {
		if (detail_ptr->magic != DETAILS_MAGIC)
			fatal("dump_all_job: job detail integrity is bad");
		pack16((uint16_t) DETAILS_FLAG, buffer);
		_dump_job_details_state(detail_ptr, buffer);
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

/*
 * _dump_job_details_state - dump the state of a specific job details to 
 *	a buffer
 * IN detail_ptr - pointer to job details for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
void _dump_job_details_state(struct job_details *detail_ptr, Buf buffer)
{
	char tmp_str[MAX_STR_PACK];

	pack_job_credential(&detail_ptr->credential, buffer);

	pack32((uint32_t) detail_ptr->num_procs, buffer);
	pack32((uint32_t) detail_ptr->num_nodes, buffer);

	pack16((uint16_t) detail_ptr->shared, buffer);
	pack16((uint16_t) detail_ptr->contiguous, buffer);

	pack32((uint32_t) detail_ptr->min_procs, buffer);
	pack32((uint32_t) detail_ptr->min_memory, buffer);
	pack32((uint32_t) detail_ptr->min_tmp_disk, buffer);
	pack_time(detail_ptr->submit_time, buffer);
	pack32((uint32_t) detail_ptr->total_procs, buffer);

	if ((detail_ptr->req_nodes == NULL) ||
	    (strlen(detail_ptr->req_nodes) < MAX_STR_PACK))
		packstr(detail_ptr->req_nodes, buffer);
	else {
		strncpy(tmp_str, detail_ptr->req_nodes, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK - 1] = (char) NULL;
		packstr(tmp_str, buffer);
	}

	if (detail_ptr->features == NULL ||
	    strlen(detail_ptr->features) < MAX_STR_PACK)
		packstr(detail_ptr->features, buffer);
	else {
		strncpy(tmp_str, detail_ptr->features, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK - 1] = (char) NULL;
		packstr(tmp_str, buffer);
	}

	if ((detail_ptr->err == NULL) ||
	    (strlen(detail_ptr->err) < MAX_STR_PACK))
		packstr(detail_ptr->err, buffer);
	else {
		strncpy(tmp_str, detail_ptr->err, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK - 1] = (char) NULL;
		packstr(tmp_str, buffer);
	}

	if ((detail_ptr->in == NULL) ||
	    (strlen(detail_ptr->in) < MAX_STR_PACK))
		packstr(detail_ptr->in, buffer);
	else {
		strncpy(tmp_str, detail_ptr->in, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK - 1] = (char) NULL;
		packstr(tmp_str, buffer);
	}

	if (detail_ptr->out == NULL ||
	    strlen(detail_ptr->out) < MAX_STR_PACK)
		packstr(detail_ptr->out, buffer);
	else {
		strncpy(tmp_str, detail_ptr->out, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK - 1] = (char) NULL;
		packstr(tmp_str, buffer);
	}

	if ((detail_ptr->work_dir == NULL) ||
	    (strlen(detail_ptr->work_dir) < MAX_STR_PACK))
		packstr(detail_ptr->work_dir, buffer);
	else {
		strncpy(tmp_str, detail_ptr->work_dir, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK - 1] = (char) NULL;
		packstr(tmp_str, buffer);
	}
}

/*
 * _dump_job_step_state - dump the state of a specific job step to a buffer
 * IN detail_ptr - pointer to job step for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void _dump_job_step_state(struct step_record *step_ptr, Buf buffer)
{
	char *node_list;

	pack16((uint16_t) step_ptr->step_id, buffer);
	pack16((uint16_t) step_ptr->cyclic_alloc, buffer);
	pack32(step_ptr->num_tasks, buffer);
	pack_time(step_ptr->start_time, buffer);
	node_list = bitmap2node_name(step_ptr->node_bitmap);
	packstr(node_list, buffer);
	xfree(node_list);
#ifdef HAVE_LIBELAN3
	qsw_pack_jobinfo(step_ptr->qsw_job, buffer);
#endif
}

/*
 * load_job_state - load the job state from file, recover from last slurmctld 
 *	checkpoint. Execute this after loading the configuration file data.
 * RET 0 or error code
 */
int load_job_state(void)
{
	int data_allocated, data_read = 0, error_code = 0;
	uint32_t data_size = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	uint32_t job_id, user_id, time_limit, priority, total_procs;
	time_t buf_time, start_time, end_time, submit_time;
	uint16_t job_state, next_step_id, details;
	char *nodes = NULL, *partition = NULL, *name = NULL;
	uint32_t num_procs, num_nodes, min_procs, min_memory, min_tmp_disk;
	uint16_t shared, contiguous, batch_flag;
	uint16_t kill_on_node_fail, kill_on_step_done, name_len;
	char *req_nodes = NULL, *features = NULL;
	char *err = NULL, *in = NULL, *out = NULL, *work_dir = NULL;
	slurm_job_credential_t *credential_ptr = NULL;
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	bitstr_t *node_bitmap = NULL, *req_node_bitmap = NULL;
	uint16_t step_flag;

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
		safe_unpack32(&job_id, buffer);
		safe_unpack32(&user_id, buffer);
		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&priority, buffer);

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

		/* validity test as possible */
		if ((job_state >= JOB_END) || 
		    (kill_on_node_fail > 1) ||
		    (kill_on_step_done > 1) ||
		    (batch_flag > 1)) {
			error
			    ("Invalid data for job %u: job_state=%u  batch_flag=%u kill_on_node_fail=%u kill_on_step_done=%u",
			     job_id, job_state, batch_flag, 
			     kill_on_node_fail, kill_on_step_done);
			error
			    ("No more job data will be processed from the checkpoint file");
			FREE_NULL(nodes);
			FREE_NULL(partition);
			FREE_NULL(name);
			error_code = EINVAL;
			break;
		}

		safe_unpack16(&details, buffer);

		if (details == DETAILS_FLAG) {
			if (unpack_job_credential(&credential_ptr, buffer))
				goto unpack_error;

			safe_unpack32(&num_procs, buffer);
			safe_unpack32(&num_nodes, buffer);

			safe_unpack16(&shared, buffer);
			safe_unpack16(&contiguous, buffer);

			safe_unpack32(&min_procs, buffer);
			safe_unpack32(&min_memory, buffer);
			safe_unpack32(&min_tmp_disk, buffer);
			safe_unpack_time(&submit_time, buffer);
			safe_unpack32(&total_procs, buffer);

			safe_unpackstr_xmalloc(&req_nodes, &name_len,
					       buffer);
			safe_unpackstr_xmalloc(&features, &name_len,
					       buffer);
			safe_unpackstr_xmalloc(&err, &name_len, buffer);
			safe_unpackstr_xmalloc(&in, &name_len, buffer);
			safe_unpackstr_xmalloc(&out, &name_len, buffer);
			safe_unpackstr_xmalloc(&work_dir, &name_len,
					       buffer);

			/* validity test as possible */
			if ((shared > 1) ||
			    (contiguous > 1) || (batch_flag > 1)) {
				error
				    ("Invalid data for job %u: shared=%u contiguous=%u",
				     job_id, shared, contiguous);
				error
				    ("No more job data will be processed from the checkpoint file");
				FREE_NULL(req_nodes);
				FREE_NULL(features);
				FREE_NULL(err);
				FREE_NULL(in);
				FREE_NULL(out);
				FREE_NULL(work_dir);
				error_code = EINVAL;
				break;
			}
		}

		if (nodes) {
			error_code = node_name2bitmap(nodes, &node_bitmap);
			if (error_code) {
				error
				    ("load_job_state: invalid nodes (%s) for job_id %u",
				     nodes, job_id);
				goto cleanup;
			}
		}
		if (req_nodes) {
			error_code =
			    node_name2bitmap(req_nodes, &req_node_bitmap);
			if (error_code) {
				error
				    ("load_job_state: invalid req_nodes (%s) for job_id %u",
				     req_nodes, job_id);
				goto cleanup;
			}
		}

		job_ptr = find_job_record(job_id);
		if (job_ptr == NULL) {
			part_ptr =
			    list_find_first(part_list, &list_find_part,
					    partition);
			if (part_ptr == NULL) {
				info("load_job_state: invalid partition (%s) for job_id %u", 
				     partition, job_id);
				error_code = EINVAL;
				goto cleanup;
			}
			job_ptr = create_job_record(&error_code);
			if (error_code) {
				error
				    ("load_job_state: unable to create job entry for job_id %u",
				     job_id);
				goto cleanup;
			}
			job_ptr->job_id = job_id;
			strncpy(job_ptr->partition, partition,
				MAX_NAME_LEN);
			job_ptr->part_ptr = part_ptr;
			_add_job_hash(job_ptr);
			info("recovered job id %u", job_id);
		}

		job_ptr->user_id = user_id;
		job_ptr->time_limit = time_limit;
		job_ptr->priority = priority;
		job_ptr->start_time = start_time;
		job_ptr->end_time = end_time;
		job_ptr->time_last_active = time(NULL);
		job_ptr->job_state = job_state;
		job_ptr->next_step_id = next_step_id;
		strncpy(job_ptr->name, name, MAX_NAME_LEN);
		job_ptr->nodes = nodes;
		nodes = NULL;
		job_ptr->node_bitmap = node_bitmap;
		node_bitmap = NULL;
		job_ptr->kill_on_node_fail = kill_on_node_fail;
		job_ptr->kill_on_step_done = kill_on_step_done;
		job_ptr->batch_flag = batch_flag;
		build_node_details(job_ptr);

		if (default_prio >= priority)
			default_prio = priority - 1;
		if (job_id_sequence <= job_id)
			job_id_sequence = job_id + 1;

		if (details == DETAILS_FLAG) {
			job_ptr->details->num_procs = num_procs;
			job_ptr->details->num_nodes = num_nodes;
			job_ptr->details->shared = shared;
			job_ptr->details->contiguous = contiguous;
			job_ptr->details->min_procs = min_procs;
			job_ptr->details->min_memory = min_memory;
			job_ptr->details->min_tmp_disk = min_tmp_disk;
			job_ptr->details->submit_time = submit_time;
			job_ptr->details->total_procs = total_procs;
			job_ptr->details->req_nodes = req_nodes;
			req_nodes = NULL;
			job_ptr->details->req_node_bitmap =
			    req_node_bitmap;
			req_node_bitmap = NULL;
			job_ptr->details->features = features;
			features = NULL;
			job_ptr->details->err = err;
			err = NULL;
			job_ptr->details->in = in;
			in = NULL;
			job_ptr->details->out = out;
			out = NULL;
			job_ptr->details->work_dir = work_dir;
			work_dir = NULL;
			memcpy(&job_ptr->details->credential,
			       credential_ptr,
			       sizeof(job_ptr->details->credential));
		}

		safe_unpack16(&step_flag, buffer);
		while (step_flag == STEP_FLAG) {
			struct step_record *step_ptr;
			uint16_t step_id, cyclic_alloc;
			uint32_t num_tasks;
			time_t start_time;
			char *node_list;

			safe_unpack16(&step_id, buffer);
			safe_unpack16(&cyclic_alloc, buffer);
			safe_unpack32(&num_tasks, buffer);
			safe_unpack_time(&start_time, buffer);
			safe_unpackstr_xmalloc(&node_list, &name_len,
					       buffer);

			/* validity test as possible */
			if (cyclic_alloc > 1) {
				error
				    ("Invalid data for job %u.%u: cyclic_alloc=%u",
				     job_id, step_id, cyclic_alloc);
				error
				    ("No more job data will be processed from the checkpoint file");
				error_code = EINVAL;
				break;
			}

			step_ptr = create_step_record(job_ptr);
			if (step_ptr == NULL)
				break;
			step_ptr->step_id = step_id;
			step_ptr->cyclic_alloc = cyclic_alloc;
			step_ptr->num_tasks = num_tasks;
			step_ptr->start_time = start_time;
			info("recovered job step %u.%u", job_id, step_id);
			if (node_list) {
				(void) node_name2bitmap(node_list,
							&(step_ptr->
							  node_bitmap));
				xfree(node_list);
			}
#ifdef HAVE_LIBELAN3
			qsw_alloc_jobinfo(&step_ptr->qsw_job);
			if (qsw_unpack_jobinfo(step_ptr->qsw_job, buffer)) {
				qsw_free_jobinfo(step_ptr->qsw_job);
				goto unpack_error;
			}
#endif
			safe_unpack16(&step_flag, buffer);
		}
		if (error_code)
			break;

	      cleanup:
		FREE_NULL(nodes);
		FREE_NULL(partition);
		FREE_NULL(name);
		FREE_NULL(req_nodes);
		FREE_NULL(features);
		FREE_NULL(err);
		FREE_NULL(in);
		FREE_NULL(out);
		FREE_NULL(work_dir);
		FREE_NULL_BITMAP(node_bitmap);
		FREE_NULL_BITMAP(req_node_bitmap);
		FREE_NULL(credential_ptr);
	}

	free_buf(buffer);
	return error_code;

      unpack_error:
	error("Incomplete job data checkpoint file");
	error("Job state not completely restored");
	FREE_NULL(nodes);
	FREE_NULL(partition);
	FREE_NULL(name);
	FREE_NULL(req_nodes);
	FREE_NULL(features);
	FREE_NULL(err);
	FREE_NULL(in);
	FREE_NULL(out);
	FREE_NULL(work_dir);
	free_buf(buffer);
	return EFAULT;
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
		if (max_hash_over >= MAX_JOB_COUNT)
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
 * kill_running_job_by_node_name - Given a node name, deallocate that job 
 *	from the node or kill it 
 * IN node_name - name of a node
 * RET number of killed jobs
 */
int kill_running_job_by_node_name(char *node_name)
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	struct node_record *node_record_point;
	int bit_position;
	int job_count = 0;

	node_record_point = find_node_record(node_name);
	if (node_record_point == NULL)	/* No such node */
		return 0;
	bit_position = node_record_point - node_record_table_ptr;

	job_record_iterator = list_iterator_create(job_list);
	while ((job_record_point =
		(struct job_record *) list_next(job_record_iterator))) {
		if (job_record_point->job_state != JOB_RUNNING)
			continue;	/* job not active */
		if (bit_test(job_record_point->node_bitmap, bit_position)
		    == 0)
			continue;	/* job not on this node */

		error("Running job_id %u on failed node node %s",
		      job_record_point->job_id, node_name);
		job_count++;
		if ((job_record_point->details == NULL) ||
		    (job_record_point->kill_on_node_fail)) {
			last_job_update = time(NULL);
			job_record_point->job_state = JOB_NODE_FAIL;
			job_record_point->end_time = time(NULL);
			deallocate_nodes(job_record_point);
			delete_job_details(job_record_point);
		}

	}
	list_iterator_destroy(job_record_iterator);

	return job_count;
}



/*
 * dump_job_desc - dump the incoming job submit request message
 * IN job_specs - job specification from RPC
 */
void dump_job_desc(job_desc_msg_t * job_specs)
{
	long job_id, min_procs, min_memory, min_tmp_disk, num_procs;
	long num_nodes, time_limit, priority, contiguous;
	long kill_on_node_fail, shared;

	if (job_specs == NULL)
		return;

	job_id = (job_specs->job_id != NO_VAL) ? job_specs->job_id : -1;
	debug3("JobDesc: user_id=%u job_id=%ld partition=%s name=%s",
	       job_specs->user_id, job_id,
	       job_specs->partition, job_specs->name);

	min_procs =
	    (job_specs->min_procs != NO_VAL) ? job_specs->min_procs : -1;
	min_memory =
	    (job_specs->min_memory != NO_VAL) ? job_specs->min_memory : -1;
	min_tmp_disk =
	    (job_specs->min_tmp_disk !=
	     NO_VAL) ? job_specs->min_tmp_disk : -1;
	debug3
	    ("   min_procs=%ld min_memory=%ld min_tmp_disk=%ld features=%s",
	     min_procs, min_memory, min_tmp_disk, job_specs->features);

	num_procs =
	    (job_specs->num_procs != NO_VAL) ? job_specs->num_procs : -1;
	num_nodes =
	    (job_specs->num_nodes != NO_VAL) ? job_specs->num_nodes : -1;
	debug3("   num_procs=%ld num_nodes=%ld req_nodes=%s", num_procs,
	       num_nodes, job_specs->req_nodes);

	time_limit =
	    (job_specs->time_limit != NO_VAL) ? job_specs->time_limit : -1;
	priority =
	    (job_specs->priority != NO_VAL) ? job_specs->priority : -1;
	contiguous =
	    (job_specs->contiguous !=
	     (uint16_t) NO_VAL) ? job_specs->contiguous : -1;
	kill_on_node_fail =
	    (job_specs->kill_on_node_fail !=
	     (uint16_t) NO_VAL) ? job_specs->kill_on_node_fail : -1;
	shared =
	    (job_specs->shared !=
	     (uint16_t) NO_VAL) ? job_specs->shared : -1;
	debug3("   time_limit=%ld priority=%ld contiguous=%ld shared=%ld",
	       time_limit, priority, contiguous, shared);

	debug3("   kill_on_node_fail=%ld task_dist=%u script=%.40s...",
	       kill_on_node_fail, job_specs->task_dist, job_specs->script);

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

	debug3("   in=%s out=%s err=%s work_dir=%s",
	       job_specs->in, job_specs->out, job_specs->err,
	       job_specs->work_dir);

}


/* 
 * init_job_conf - initialize the job configuration tables and values. 
 *	this should be called after creating node information, but 
 *	before creating any job entries.
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
			fatal
			    ("init_job_conf: list_create can not allocate memory");
	}
	last_job_update = time(NULL);
	return SLURM_SUCCESS;
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
	int error_code, test_only;
	struct job_record *job_ptr;

	error_code = _job_create(job_specs, new_job_id, allocate, will_run,
				 &job_ptr, submit_uid);
	if (error_code)
		return error_code;
	if (job_ptr == NULL)
		fatal("job_allocate: allocated job %u lacks record",
		      new_job_id);

	if (immediate && _top_priority(job_ptr) != 1) {
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time = 0;
		return ESLURM_NOT_TOP_PRIORITY;
	}

	test_only = will_run || (allocate == 0);
	if (test_only == 0) {
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

	error_code = select_nodes(job_ptr, test_only);
	if (error_code == ESLURM_NODES_BUSY) {
		if (immediate) {
			job_ptr->job_state = JOB_FAILED;
			job_ptr->end_time = 0;
		} else		/* job remains queued */
			error_code = 0;
		return error_code;
	}

	if (error_code) {	/* fundamental flaw in job request */
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time = 0;
		return error_code;
	}

	if (will_run) {		/* job would run, flag job destruction */
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time = 0;
	}

	if (test_only == 0) {
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


/* 
 * job_cancel - cancel the specified job
 * IN job_id - id of the job to be cancelled
 * IN uid - uid of requesting user
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int job_cancel(uint32_t job_id, uid_t uid)
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("job_cancel: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->job_state == JOB_FAILED) ||
	    (job_ptr->job_state == JOB_COMPLETE) ||
	    (job_ptr->job_state == JOB_TIMEOUT))
		return ESLURM_ALREADY_DONE;

	if ((job_ptr->user_id != uid) && (uid != 0) && (uid != getuid())) {
		error("Security violation, JOB_CANCEL RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (job_ptr->job_state == JOB_PENDING) {
		last_job_update = time(NULL);
		job_ptr->job_state = JOB_FAILED;
		job_ptr->start_time = job_ptr->end_time = time(NULL);
		delete_job_details(job_ptr);
		verbose("job_cancel of pending job %u successful", job_id);
		return SLURM_SUCCESS;
	}

	if (job_ptr->job_state == JOB_RUNNING) {
		last_job_update = time(NULL);
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time = time(NULL);
		deallocate_nodes(job_ptr);
		delete_all_step_records(job_ptr);
		delete_job_details(job_ptr);
		verbose("job_cancel of running job %u successful", job_id);
		return SLURM_SUCCESS;
	}

	verbose("job_cancel: job %u can't be cancelled from state=%s",
		job_id, job_state_string(job_ptr->job_state));
	return ESLURM_TRANSITION_STATE_NO_UPDATE;
}

/* 
 * job_complete - note the normal termination the specified job
 * IN job_id - id of the job which completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN job_return_code - job's return code, if set then set state to JOB_FAILED
 * RET - 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int
job_complete(uint32_t job_id, uid_t uid, bool requeue,
	     uint32_t job_return_code)
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("job_complete: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->job_state == JOB_FAILED) ||
	    (job_ptr->job_state == JOB_COMPLETE) ||
	    (job_ptr->job_state == JOB_TIMEOUT))
		return ESLURM_ALREADY_DONE;

	if ((job_ptr->user_id != uid) && (uid != 0) && (uid != getuid())) {
		error("Security violation, JOB_COMPLETE RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (job_ptr->job_state == JOB_PENDING) {
		verbose("job_complete for job id %u successful", job_id);
	} else if (job_ptr->job_state == JOB_RUNNING) {
		deallocate_nodes(job_ptr);
		verbose("job_complete for job id %u successful", job_id);
	} else {
		error("job_complete for job id %u from bad state",
		      job_id, job_ptr->job_state);
	}

	if (requeue && job_ptr->details && job_ptr->batch_flag) {
		job_ptr->job_state = JOB_PENDING;
		info("Requeing job %u", job_ptr->job_id);
	} else {
		if (job_return_code)
			job_ptr->job_state = JOB_FAILED;
		else
			job_ptr->job_state = JOB_COMPLETE;
		job_ptr->end_time = time(NULL);
		delete_job_details(job_ptr);
		delete_all_step_records(job_ptr);
	}
	last_job_update = time(NULL);
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
	int error_code, i;
	struct part_record *part_ptr;
	bitstr_t *req_bitmap = NULL;

	if ((error_code = _validate_job_desc(job_desc, allocate)))
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
	if (job_desc->time_limit == NO_VAL)
		/* Default time_limit is partition maximum */
		job_desc->time_limit = part_ptr->max_time;


	/* can this user access this partition */
	if ((part_ptr->root_only) && (submit_uid != 0)) {
		error
		    ("_job_create: non-root job submission to partition %s by uid %u",
		     part_ptr->name, (unsigned int) submit_uid);
		error_code = ESLURM_ACCESS_DENIED;
		return error_code;
	}
	if (validate_group(part_ptr, submit_uid) == 0) {
		info("_job_create: job lacks group required of partition %s, uid %u", 
		     part_ptr->name, (unsigned int) submit_uid);
		error_code = ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP;
		return error_code;
	}

	/* check if select partition has sufficient resources to satisfy
	 * the request */

	/* insure that selected nodes are in this partition */
	if (job_desc->req_nodes) {
		error_code =
		    node_name2bitmap(job_desc->req_nodes, &req_bitmap);
		if (error_code == EINVAL)
			goto cleanup;
		if (error_code != 0) {
			error_code = EAGAIN;	/* no memory */
			goto cleanup;
		}
		if (job_desc->contiguous)
			bit_fill_gaps(req_bitmap);
		if (bit_super_set(req_bitmap, part_ptr->node_bitmap) != 1) {
			info("_job_create: requested nodes %s not in partition %s", 
			     job_desc->req_nodes, part_ptr->name);
			error_code =
			    ESLURM_REQUESTED_NODES_NOT_IN_PARTITION;
			goto cleanup;
		}
		i = count_cpus(req_bitmap);
		if (i > job_desc->num_procs)
			job_desc->num_procs = i;
		i = bit_set_count(req_bitmap);
		if (i > job_desc->num_nodes)
			job_desc->num_nodes = i;
	}
	if (job_desc->num_procs > part_ptr->total_cpus) {
		info("_job_create: too many cpus (%d) requested of partition %s(%d)", 
		     job_desc->num_procs, part_ptr->name, 
		     part_ptr->total_cpus);
		error_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
		goto cleanup;
	}
	if ((job_desc->num_nodes > part_ptr->total_nodes) ||
	    (job_desc->num_nodes > part_ptr->max_nodes)) {
		if (part_ptr->total_nodes > part_ptr->max_nodes)
			i = part_ptr->max_nodes;
		else
			i = part_ptr->total_nodes;
		info("_job_create: too many nodes (%d) requested of partition %s(%d)", 
		     job_desc->num_nodes, part_ptr->name, i);
		error_code = ESLURM_TOO_MANY_REQUESTED_NODES;
		goto cleanup;
	}

	if ((error_code =_validate_job_create_req(job_desc)))
		goto cleanup;

	if (will_run) {
		error_code = 0;
		goto cleanup;
	}

	if ((error_code = _copy_job_desc_to_job_record(job_desc,
						       job_rec_ptr,
						       part_ptr,
						       req_bitmap))) {
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

	if (part_ptr->shared == SHARED_FORCE)	/* shared=force */
		(*job_rec_ptr)->details->shared = 1;
	else if (((*job_rec_ptr)->details->shared != 1) || 
	         (part_ptr->shared == SHARED_NO))	/* can't share */
		(*job_rec_ptr)->details->shared = 0;

	*new_job_id = (*job_rec_ptr)->job_id;
	return SLURM_SUCCESS;

      cleanup:
	FREE_NULL_BITMAP(req_bitmap);
	return error_code;
}

/* Perform some size checks on strings we store to prevent
 * malicious user filling slurmctld's memory
 * RET 0 or error code */
static int _validate_job_create_req(job_desc_msg_t * job_desc)
{
	if (job_desc->err && (strlen(job_desc->err) > BUF_SIZE)) {
		info("_job_create: strlen(err) too big (%d)",
		     strlen(job_desc->err));
		return ESLURM_PATHNAME_TOO_LONG;
	}
	if (job_desc->in && (strlen(job_desc->in) > BUF_SIZE)) {
		info("_job_create: strlen(in) too big (%d)",
		     strlen(job_desc->in));
		return  ESLURM_PATHNAME_TOO_LONG;
	}
	if (job_desc->out && (strlen(job_desc->out) > BUF_SIZE)) {
		info("_job_create: strlen(out) too big (%d)",
		     strlen(job_desc->out));
		return  ESLURM_PATHNAME_TOO_LONG;
	}
	if (job_desc->work_dir && (strlen(job_desc->work_dir) > BUF_SIZE)) {
		info("_job_create: strlen(work_dir) too big (%d)",
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

	if (data == NULL) {
		(void) unlink(file_name);
		return SLURM_SUCCESS;
	}

	fd = creat(file_name, 0600);
	if (fd < 0) {
		error("Error creating file %s, %m", file_name);
		return ESLURM_WRITING_TO_FILE;
	}

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

	if ((file_name == NULL) || (data == NULL) || (size == NULL))
		fatal("_read_data_array_from_file passed NULL pointer");
	*data = NULL;
	*size = 0;

	fd = open(file_name, 0);
	if (fd < 0) {
		error("Error opening file %s, %m", file_name);
		return;
	}

	amount = read(fd, &rec_cnt, sizeof(uint16_t));
	if (amount < sizeof(uint16_t)) {
		error("Error reading file %s, %m", file_name);
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

	if ((file_name == NULL) || (data == NULL))
		fatal("_read_data_from_file passed NULL pointer");
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
			     bitstr_t * req_bitmap)
{
	int error_code;
	struct job_details *detail_ptr;
	struct job_record *job_ptr;

	job_ptr = create_job_record(&error_code);
	if (error_code)
		return error_code;

	strncpy(job_ptr->partition, part_ptr->name, MAX_NAME_LEN);
	job_ptr->part_ptr = part_ptr;
	if (job_desc->job_id != NO_VAL)
		job_ptr->job_id = job_desc->job_id;
	else
		_set_job_id(job_ptr);
	_add_job_hash(job_ptr);

	if (job_desc->name) {
		strncpy(job_ptr->name, job_desc->name,
			sizeof(job_ptr->name));
	}
	job_ptr->user_id = (uid_t) job_desc->user_id;
	job_ptr->job_state = JOB_PENDING;
	job_ptr->time_limit = job_desc->time_limit;
	if ((job_desc->priority !=
	     NO_VAL) /* also check submit UID is root */ )
		job_ptr->priority = job_desc->priority;
	else
		_set_job_prio(job_ptr);
	if (job_desc->kill_on_node_fail != (uint16_t) NO_VAL)
		job_ptr->kill_on_node_fail = job_desc->kill_on_node_fail;

	detail_ptr = job_ptr->details;
	detail_ptr->num_procs = job_desc->num_procs;
	detail_ptr->num_nodes = job_desc->num_nodes;
	if (job_desc->req_nodes) {
		detail_ptr->req_nodes = xstrdup(job_desc->req_nodes);
		detail_ptr->req_node_bitmap = req_bitmap;
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
 * build_job_cred - build a credential for a job, only valid after 
 *	allocation made
 * IN job_ptr - pointer to the job record 
 */
void build_job_cred(struct job_record *job_ptr)
{
	struct job_details *detail_ptr;
	if (job_ptr == NULL)
		return;

	detail_ptr = job_ptr->details;
	if (detail_ptr == NULL)
		return;

	detail_ptr->credential.job_id = job_ptr->job_id;
	detail_ptr->credential.user_id = job_ptr->user_id;
	detail_ptr->credential.node_list = xstrdup(job_ptr->nodes);
	detail_ptr->credential.expiration_time = job_ptr->end_time;
	if (sign_credential(&sign_ctx, &detail_ptr->credential)) {
		error("Error building credential for job_id %u: %m",
		      job_ptr->job_id);
	}
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
		if (job_ptr->magic != JOB_MAGIC)
			fatal("job_time_limit: job integrity is bad");
		if ((job_ptr->job_state == JOB_PENDING) ||
		    (job_ptr->job_state == JOB_FAILED) ||
		    (job_ptr->job_state == JOB_COMPLETE) ||
		    (job_ptr->job_state == JOB_TIMEOUT) ||
		    (job_ptr->job_state == JOB_NODE_FAIL))
			continue;
		if (slurmctld_conf.inactive_limit) {
			if (job_ptr->step_list &&
			    (list_count(job_ptr->step_list) > 0))
				job_ptr->time_last_active = now;
			else if ((job_ptr->time_last_active +
				  slurmctld_conf.inactive_limit) <= now) {
				/* job inactive, kill it */
				job_ptr->end_time = now;
				job_ptr->time_limit = 1;
			}
		}
		if ((job_ptr->time_limit == INFINITE) ||
		    (job_ptr->end_time > now))
			continue;
		last_job_update = now;
		info("Time limit exhausted for job_id %u, terminated",
		     job_ptr->job_id);
		job_ptr->job_state = JOB_TIMEOUT;
		job_ptr->end_time = time(NULL);
		deallocate_nodes(job_ptr);
		delete_all_step_records(job_ptr);
		delete_job_details(job_ptr);
	}

	list_iterator_destroy(job_record_iterator);
}

/* _validate_job_desc - validate that a job descriptor for job submit or 
 *	allocate has valid data, set values to defaults as required 
 * IN job_desc_msg - pointer to job descriptor
 * IN allocate - if clear job to be queued, if set allocate for user now 
 */
static int _validate_job_desc(job_desc_msg_t * job_desc_msg, int allocate)
{
	if ((job_desc_msg->num_procs == NO_VAL) &&
	    (job_desc_msg->num_nodes == NO_VAL) &&
	    (job_desc_msg->req_nodes == NULL)) {
		info("_job_create: job failed to specify ReqNodes, TotalNodes or TotalProcs");
		return ESLURM_JOB_MISSING_SIZE_SPECIFICATION;
	}
	if ((allocate == SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0) &&
	    (job_desc_msg->script == NULL)) {
		info("_job_create: job failed to specify Script");
		return ESLURM_JOB_SCRIPT_MISSING;
	}
	if (job_desc_msg->user_id == NO_VAL) {
		info("_job_create: job failed to specify User");
		return ESLURM_USER_ID_MISSING;
	}
	if ((job_desc_msg->name) &&
	    (strlen(job_desc_msg->name) > MAX_NAME_LEN)) {
		info("_job_create: job name %s too long",
		     job_desc_msg->name);
		return ESLURM_JOB_NAME_TOO_LONG;
	}
	if (job_desc_msg->contiguous == NO_VAL)
		job_desc_msg->contiguous = 0;
	if (job_desc_msg->kill_on_node_fail == NO_VAL)
		job_desc_msg->kill_on_node_fail = 1;
	if (job_desc_msg->shared == NO_VAL)
		job_desc_msg->shared = 0;

	if ((job_desc_msg->job_id != NO_VAL) &&
	    (find_job_record((uint32_t) job_desc_msg->job_id))) {
		info("_job_create: Duplicate job id %d",
		     job_desc_msg->job_id);
		return ESLURM_DUPLICATE_JOB_ID;
	}
	if (job_desc_msg->num_procs == NO_VAL)
		job_desc_msg->num_procs = 1;	/* default cpu count of 1 */
	if (job_desc_msg->num_nodes == NO_VAL)
		job_desc_msg->num_nodes = 1;	/* default node count of 1 */
	if (job_desc_msg->min_memory == NO_VAL)
		job_desc_msg->min_memory = 1;	/* default 1 MB mem per node */
	if (job_desc_msg->min_tmp_disk == NO_VAL)
		job_desc_msg->min_tmp_disk = 1;	/* default 1 MB disk per node */
	if (job_desc_msg->shared == NO_VAL)
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
	struct job_record *job_record_point;
	int i, j;

	job_record_point = (struct job_record *) job_entry;
	if (job_record_point == NULL)
		fatal("_list_delete_job: passed null job pointer");
	if (job_record_point->magic != JOB_MAGIC)
		fatal("_list_delete_job: passed invalid job pointer");

	if (job_hash[JOB_HASH_INX(job_record_point->job_id)] ==
	    job_record_point)
		job_hash[JOB_HASH_INX(job_record_point->job_id)] = NULL;
	else {
		for (i = 0; i < max_hash_over; i++) {
			if (job_hash_over[i] != job_record_point)
				continue;
			for (j = i + 1; j < max_hash_over; j++) {
				job_hash_over[j - 1] = job_hash_over[j];
			}
			job_hash_over[--max_hash_over] = NULL;
			break;
		}
	}

	delete_job_details(job_record_point);

	FREE_NULL(job_record_point->nodes);
	FREE_NULL_BITMAP(job_record_point->node_bitmap);
	FREE_NULL(job_record_point->node_addr);
	if (job_record_point->step_list) {
		delete_all_step_records(job_record_point);
		list_destroy(job_record_point->step_list);
	}
	job_count--;
	xfree(job_record_point);
}


/*
 * _list_find_job_old - find an entry in the job list,  
 *	see common/list.h for documentation, key is ignored 
 * global- job_list - the global partition list
 */
int _list_find_job_old(void *job_entry, void *key)
{
	time_t min_age;

	min_age = time(NULL) - MIN_JOB_AGE;

	if (((struct job_record *) job_entry)->end_time > min_age)
		return 0;

	if ((((struct job_record *) job_entry)->job_state != JOB_COMPLETE)
	    && (((struct job_record *) job_entry)->job_state != JOB_FAILED)
	    && (((struct job_record *) job_entry)->job_state !=
		JOB_TIMEOUT))
		return 0;

	return 1;
}


/* 
 * pack_all_jobs - dump all job information for all jobs in 
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN/OUT update_time - dump new data only if job records updated since time 
 * 	specified, otherwise return empty buffer, set to time partition 
 *	records last updated
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c 
 *	whenever the data format changes
 */
void
pack_all_jobs(char **buffer_ptr, int *buffer_size, time_t * update_time)
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	uint32_t jobs_packed = 0, tmp_offset;
	Buf buffer;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	if (*update_time == last_job_update)
		return;

	buffer = init_buf(BUF_SIZE * 16);

	/* write message body header : size and time */
	/* put in a place holder job record count of 0 for now */
	pack32((uint32_t) jobs_packed, buffer);
	pack_time(last_job_update, buffer);

	/* write individual job records */
	job_record_iterator = list_iterator_create(job_list);
	while ((job_record_point =
		(struct job_record *) list_next(job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal("dump_all_job: job integrity is bad");

		pack_job(job_record_point, buffer);
		jobs_packed++;
	}

	list_iterator_destroy(job_record_iterator);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32((uint32_t) jobs_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*update_time = last_job_update;
	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}


/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission)
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	  whenever the data format changes
 */
void pack_job(struct job_record *dump_job_ptr, Buf buffer)
{
	char tmp_str[MAX_STR_PACK];
	struct job_details *detail_ptr;

	pack32(dump_job_ptr->job_id, buffer);
	pack32(dump_job_ptr->user_id, buffer);

	pack16((uint16_t) dump_job_ptr->job_state, buffer);
	pack16((uint16_t) dump_job_ptr->batch_flag, buffer);
	pack32(dump_job_ptr->time_limit, buffer);

	pack_time(dump_job_ptr->start_time, buffer);
	pack_time(dump_job_ptr->end_time, buffer);
	pack32(dump_job_ptr->priority, buffer);

	packstr(dump_job_ptr->nodes, buffer);
	packstr(dump_job_ptr->partition, buffer);
	packstr(dump_job_ptr->name, buffer);
	if (dump_job_ptr->node_bitmap) {
		(void) bit_fmt(tmp_str, MAX_STR_PACK,
			       dump_job_ptr->node_bitmap);
		packstr(tmp_str, buffer);
	} else
		packstr(NULL, buffer);

	detail_ptr = dump_job_ptr->details;
	if (detail_ptr && dump_job_ptr->job_state == JOB_PENDING) {
		if (detail_ptr->magic != DETAILS_MAGIC)
			fatal("dump_all_job: job detail integrity is bad");
		pack32((uint32_t) detail_ptr->num_procs, buffer);
		pack32((uint32_t) detail_ptr->num_nodes, buffer);
		pack16((uint16_t) detail_ptr->shared, buffer);
		pack16((uint16_t) detail_ptr->contiguous, buffer);

		pack32((uint32_t) detail_ptr->min_procs, buffer);
		pack32((uint32_t) detail_ptr->min_memory, buffer);
		pack32((uint32_t) detail_ptr->min_tmp_disk, buffer);

		if ((detail_ptr->req_nodes == NULL) ||
		    (strlen(detail_ptr->req_nodes) < MAX_STR_PACK))
			packstr(detail_ptr->req_nodes, buffer);
		else {
			strncpy(tmp_str, detail_ptr->req_nodes,
				MAX_STR_PACK);
			tmp_str[MAX_STR_PACK - 1] = (char) NULL;
			packstr(tmp_str, buffer);
		}

		if (detail_ptr->req_node_bitmap) {
			(void) bit_fmt(tmp_str, MAX_STR_PACK,
				       detail_ptr->req_node_bitmap);
			packstr(tmp_str, buffer);
		} else
			packstr(NULL, buffer);

		if (detail_ptr->features == NULL ||
		    strlen(detail_ptr->features) < MAX_STR_PACK)
			packstr(detail_ptr->features, buffer);
		else {
			strncpy(tmp_str, detail_ptr->features,
				MAX_STR_PACK);
			tmp_str[MAX_STR_PACK - 1] = (char) NULL;
			packstr(tmp_str, buffer);
		}
	} else {
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
		info("purge_old_job: purged %d old job records", i);
		last_job_update = time(NULL);
	}
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
	struct job_record *job_record_point;

	if (job_list == NULL)
		fatal
		    ("init_job_conf: list_create can not allocate memory");

	job_record_iterator = list_iterator_create(job_list);
	while ((job_record_point =
		(struct job_record *) list_next(job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal("dump_all_job: job integrity is bad");
		FREE_NULL_BITMAP(job_record_point->node_bitmap);
		if (job_record_point->nodes) {
			node_name2bitmap(job_record_point->nodes,
					 &job_record_point->node_bitmap);
			if (job_record_point->job_state == JOB_RUNNING)
				allocate_nodes(job_record_point->
					       node_bitmap);

		}

		if (job_record_point->details == NULL)
			continue;
		FREE_NULL_BITMAP(job_record_point->details->req_node_bitmap);
		if (job_record_point->details->req_nodes)
			node_name2bitmap(job_record_point->details->
					 req_nodes,
					 &job_record_point->details->
					 req_node_bitmap);
	}

	list_iterator_destroy(job_record_iterator);
	last_job_update = time(NULL);
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

	if ((job_ptr == NULL) || (job_ptr->magic != JOB_MAGIC))
		fatal("_set_job_id: invalid job_ptr");
	if ((job_ptr->partition == NULL)
	    || (strlen(job_ptr->partition) == 0))
		fatal("_set_job_id: partition not set");

	/* Include below code only if fear of rolling over 32 bit job IDs */
	while (1) {
		new_id = job_id_sequence++;
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
	if ((job_ptr == NULL) || (job_ptr->magic != JOB_MAGIC))
		fatal("_set_job_prio: invalid job_ptr");
	job_ptr->priority = default_prio--;
}


/* 
 * _top_priority - determine if any other job for this partition has a 
 *	higher priority than specified job
 * IN job_ptr - pointer to selected partition
 * RET 1 if selected job has highest priority, 0 otherwise
 */
static int _top_priority(struct job_record *job_ptr)
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	int top;

	top = 1;		/* assume top priority until found otherwise */
	job_record_iterator = list_iterator_create(job_list);
	while ((job_record_point =
		(struct job_record *) list_next(job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal("_top_priority: job integrity is bad");
		if (job_record_point == job_ptr)
			continue;
		if (job_record_point->job_state != JOB_PENDING)
			continue;
		if (job_record_point->priority > job_ptr->priority &&
		    job_record_point->part_ptr == job_ptr->part_ptr) {
			top = 0;
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
 * RET returns an error code from common/slurm_errno.h
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
	last_job_update = time(NULL);

	if (job_specs->time_limit != NO_VAL) {
		if (super_user ||
		    (job_ptr->time_limit > job_specs->time_limit)) {
			job_ptr->time_limit = job_specs->time_limit;
			job_ptr->end_time =
			    job_ptr->start_time +
			    (job_ptr->time_limit * 60);
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

	if (job_specs->num_nodes != NO_VAL && detail_ptr) {
		if (super_user ||
		    (detail_ptr->num_nodes > job_specs->num_nodes)) {
			detail_ptr->num_nodes = job_specs->num_nodes;
			info("update_job: setting num_nodes to %u for job_id %u", 
			     job_specs->num_nodes, job_specs->job_id);
		} else {
			error("Attempt to increase num_nodes for job %u",
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
			FREE_NULL(detail_ptr->features);
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
				FREE_NULL(detail_ptr->req_nodes);
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
 *	records, call this function after validate_node_specs() sets the node 
 *	state properly 
 * IN node_name - node which should have jobs running
 * IN job_count - number of jobs which should be running on specified node
 * IN job_id_ptr - pointer to array of job_ids that should be on this node
 * IN step_id_ptr - pointer to array of job step ids that should be on node
 */
void
validate_jobs_on_node(char *node_name, uint32_t * job_count,
		      uint32_t * job_id_ptr, uint16_t * step_id_ptr)
{
	int i, node_inx, jobs_running = 0;
	struct node_record *node_ptr;
	struct job_record *job_ptr;

	node_ptr = find_node_record(node_name);
	if (node_ptr == NULL) {
		error("slurmd registered on unknown node %s", node_name);
		return;
	}
	node_inx = node_ptr - node_record_table_ptr;

	/* If no job is running here, ensure none are assigned to this node */
	if (*job_count == 0) {
		(void) kill_running_job_by_node_name(node_name);
		return;
	}

	/* Ensure that jobs which are running are really supposed to be there */
	for (i = 0; i < *job_count; i++) {
		job_ptr = find_job_record(job_id_ptr[i]);
		if (job_ptr == NULL) {
			/* FIXME: In the future try to let job run */
			error("Orphan job_id %u reported on node %s",
			      job_id_ptr[i], node_name);
			_signal_job_on_node(job_id_ptr[i], step_id_ptr[i],
					    SIGKILL, node_name);
			/* We may well have pending purge job RPC to send 
			 * slurmd, which would synchronize this */
		}

		else if (job_ptr->job_state == JOB_RUNNING) {
			if (bit_test(job_ptr->node_bitmap, node_inx)) {
				jobs_running++;
				debug3("Registered job_id %u on node %s ",
				       job_id_ptr[i], node_name);
			} else {
				error
				    ("REGISTERED JOB_ID %u ON WRONG NODE %s ",
				     job_id_ptr[i], node_name);
				_signal_job_on_node(job_id_ptr[i],
						    step_id_ptr[i],
						    SIGKILL, node_name);
			}
		}

		else if (job_ptr->job_state == JOB_PENDING) {
			/* FIXME: In the future try to let job run */
			error("REGISTERED PENDING JOB_ID %u ON NODE %s ",
			      job_id_ptr[i], node_name);
			job_ptr->job_state = JOB_FAILED;
			last_job_update = time(NULL);
			job_ptr->end_time = time(NULL);
			delete_job_details(job_ptr);
			_signal_job_on_node(job_id_ptr[i], step_id_ptr[i],
					    SIGKILL, node_name);
		}

		else {		/* else job is supposed to be done */
			error
			    ("Registered job_id %u in state %s on node %s ",
			     job_id_ptr[i],
			     job_state_string(job_ptr->job_state),
			     node_name);
			_signal_job_on_node(job_id_ptr[i], step_id_ptr[i],
					    SIGKILL, node_name);
			/* We may well have pending purge job RPC to send 
			 * slurmd, which would synchronize this */
		}
	}

	if (jobs_running == 0) {	/* *job_count is > 0 */
		error("resetting job_count on node %s to zero", node_name);
		*job_count = 0;
	}

	return;
}

/* _signal_job_on_node - send specific signal to specific job_id, step_id 
 *	and node_name */
static void
_signal_job_on_node(uint32_t job_id, uint16_t step_id, int signum,
		    char *node_name)
{
	/* FIXME: add code to send RPC to specified node */
	debug("Signal %d send to job %u.%u on node %s",
	      signum, job_id, step_id, node_name);
	error("CODE DEVELOPMENT NEEDED HERE");
}


/*
 * old_job_info - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_id - ID of job for which info is requested
 * OUT everything else - the job's detains
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
	if (job_ptr->job_state == JOB_PENDING)
		return ESLURM_JOB_PENDING;
	if (job_ptr->job_state != JOB_RUNNING)
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
