/*****************************************************************************\
 *  job_mgr.c - manage the job information of slurm
 *	Note: there is a global job list (job_list), job_count, time stamp 
 *	(last_job_update), and hash table (job_hash, job_hash_over, max_hash_over)
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
#  include <config.h>
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
#include <elan3/elan3.h>
#include <elan3/elanvp.h>
#include <src/common/qsw.h>
#define BUF_SIZE (1024 + QSW_PACK_SIZE)
#else
#define BUF_SIZE 1024
#endif

#include <src/common/list.h>
#include <src/common/macros.h>
#include <src/common/pack.h>
#include <src/common/slurm_errno.h>
#include <src/common/xstring.h>
#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>

#include <src/common/credential_utils.h>
slurm_ssl_key_ctx_t sign_ctx ;

#define DETAILS_FLAG 0xdddd
#define MAX_STR_PACK 128
#define SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 0
#define STEP_FLAG 0xbbbb
#define TOP_PRIORITY 100000;

#define job_hash_inx(job_id)	(job_id % MAX_JOB_COUNT)
#define yes_or_no(in_string) \
		(( strcmp ((in_string),"YES"))? \
			(strcmp((in_string),"NO")? \
				-1 : 0 ) : 1 ) 

static int default_prio = TOP_PRIORITY;
static int job_count;			/* job's in the system */
static long job_id_sequence = -1;	/* first job_id to assign new job */
List job_list = NULL;		/* job_record list */
time_t last_job_update;		/* time of last update to job records */
static struct job_record *job_hash[MAX_JOB_COUNT];
static struct job_record *job_hash_over[MAX_JOB_COUNT];
static int max_hash_over = 0;

void 	add_job_hash (struct job_record *job_ptr);
int 	copy_job_desc_to_file ( job_desc_msg_t * job_desc , uint32_t job_id ) ;
int 	copy_job_desc_to_job_record ( job_desc_msg_t * job_desc , 
		struct job_record ** job_ptr , struct part_record *part_ptr, 
		bitstr_t *req_bitmap) ;
void	delete_job_desc_files (uint32_t job_id);
void 	dump_job_state (struct job_record *dump_job_ptr, Buf buffer);
void 	dump_job_details_state (struct job_details *detail_ptr, Buf buffer);
void 	dump_job_step_state (struct step_record *step_ptr, Buf buffer);
int	job_create (job_desc_msg_t * job_specs, uint32_t *new_job_id, int allocate, 
	    int will_run, struct job_record **job_rec_ptr, uid_t submit_uid);
void	list_delete_job (void *job_entry);
int	list_find_job_id (void *job_entry, void *key);
int	list_find_job_old (void *job_entry, void *key);
void	read_data_from_file ( char * file_name, char ** data);
void	read_data_array_from_file ( char * file_name, char *** data, uint16_t *size );
void	signal_job_on_node (uint32_t job_id, uint16_t step_id, int signum, char *node_name);
int	top_priority (struct job_record *job_ptr);
int 	validate_job_desc ( job_desc_msg_t * job_desc_msg , int allocate ) ;
int	write_data_to_file ( char * file_name, char * data ) ;
int	write_data_array_to_file ( char * file_name, char ** data, uint16_t size ) ;

/* 
 * create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * input: error_code - location to store error value in
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or NULL if error
 * global: job_list - global job list
 *	job_count - number of jobs in the system
 *	last_job_update - time of last job table update
 * NOTE: allocates memory that should be xfreed with list_delete_job
 */
struct job_record * 
create_job_record (int *error_code) 
{
	struct job_record  *job_record_point;
	struct job_details *job_details_point;

	if (job_count >= MAX_JOB_COUNT) {
		error ("create_job_record: job_count exceeds limit"); 
		*error_code = EAGAIN;
		return NULL;
	}

	job_count++;
	*error_code = 0;
	last_job_update = time (NULL);

	job_record_point =
		(struct job_record *)  xmalloc (sizeof (struct job_record));
	job_details_point =
		(struct job_details *) xmalloc (sizeof (struct job_details));

	job_record_point->magic   = JOB_MAGIC;
	job_record_point->details = job_details_point;
	job_record_point->step_list = list_create(NULL);
	if (job_record_point->step_list == NULL)
		fatal ("list_create can not allocate memory");

	job_details_point->magic  = DETAILS_MAGIC;
	job_details_point->submit_time = time (NULL);

	if (list_append (job_list, job_record_point) == NULL)
		fatal ("create_job_record: unable to allocate memory");

	return job_record_point;
}


/* 
 * delete_job_details - delete a job's detail record and clear it's pointer
 *	this information can be deleted as soon as the job is allocated resources
 * input: job_entry - pointer to job_record to clear the record of
 */
void 
delete_job_details (struct job_record *job_entry)
{
	if (job_entry->details == NULL) 
		return;

	delete_job_desc_files (job_entry->job_id);
	if (job_entry->details->magic != DETAILS_MAGIC)
		fatal ("delete_job_details: passed invalid job details pointer");
	if (job_entry->details->req_nodes)
		xfree(job_entry->details->req_nodes);
	if (job_entry->details->req_node_bitmap)
		bit_free(job_entry->details->req_node_bitmap);
	if (job_entry->details->features)
		xfree(job_entry->details->features);
	if (job_entry->details->stderr)
		xfree(job_entry->details->stderr);
	if (job_entry->details->stdin)
		xfree(job_entry->details->stdin);
	if (job_entry->details->stdout)
		xfree(job_entry->details->stdout);
	if (job_entry->details->work_dir)
		xfree(job_entry->details->work_dir);
	xfree(job_entry->details);
	job_entry->details = NULL;
}

/* delete_job_desc_files - delete job descriptor related files */
void
delete_job_desc_files (uint32_t job_id) 
{
	char *dir_name, job_dir[20], *file_name;
	struct stat sbuf;

	dir_name = xstrdup (slurmctld_conf . state_save_location);

	sprintf (job_dir, "/job.%d", job_id);
	xstrcat (dir_name, job_dir);

	file_name = xstrdup (dir_name);
	xstrcat (file_name, "/environment");
	(void) unlink (file_name);
	xfree (file_name);

	file_name = xstrdup (dir_name);
	xstrcat (file_name, "/script");
	(void) unlink (file_name);
	xfree (file_name);

	if (stat (dir_name, &sbuf) == 0)	/* remove job directory as needed */
		(void) rmdir2 (dir_name);
	xfree (dir_name);
}

/* dump_all_job_state - save the state of all jobs to file */
int
dump_all_job_state ( void )
{
	int error_code = 0, log_fd;
	char *old_file, *new_file, *reg_file;
	/* Locks: Read config and job */
	slurmctld_lock_t job_read_lock = { READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	Buf buffer = init_buf(BUF_SIZE*16);

	/* write header: time */
	pack_time  (time (NULL), buffer);

	/* write individual job records */
	lock_slurmctld (job_read_lock);
	job_record_iterator = list_iterator_create (job_list);		
	while ((job_record_point = (struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("dump_all_job: job integrity is bad");
		dump_job_state (job_record_point, buffer);
	}		
	unlock_slurmctld (job_read_lock);
	list_iterator_destroy (job_record_iterator);

	/* write the buffer to file */
	old_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (old_file, "/job_state.old");
	reg_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (reg_file, "/job_state");
	new_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (new_file, "/job_state.new");
	lock_state_files ();
	log_fd = creat (new_file, 0600);
	if (log_fd == 0) {
		error ("Can't save state, create file %s error %m", new_file);
		error_code = errno;
	}
	else {
		if (write (log_fd, get_buf_data(buffer), get_buf_offset(buffer)) != 
							get_buf_offset(buffer)) {
			error ("Can't save state, write file %s error %m", new_file);
			error_code = errno;
		}
		close (log_fd);
	}
	if (error_code) 
		(void) unlink (new_file);
	else {	/* file shuffle */
		(void) unlink (old_file);
		(void) link (reg_file, old_file);
		(void) unlink (reg_file);
		(void) link (new_file, reg_file);
		(void) unlink (new_file);
	}
	xfree (old_file);
	xfree (reg_file);
	xfree (new_file);
	unlock_state_files ();

	free_buf (buffer);
	return error_code;
}

/*
 * dump_job_state - dump the state of a specific job, its details, and steps to a buffer
 * dump_job_ptr (I) - pointer to job for which information is requested
 * buffer (I/O) - location to store data, pointers automatically advanced
 */
void 
dump_job_state (struct job_record *dump_job_ptr, Buf buffer) 
{
	struct job_details *detail_ptr;
	ListIterator step_record_iterator;
	struct step_record *step_record_ptr;

	/* Dump basic job info */
	pack32  (dump_job_ptr->job_id, buffer);
	pack32  (dump_job_ptr->user_id, buffer);
	pack32  (dump_job_ptr->time_limit, buffer);
	pack32  (dump_job_ptr->priority, buffer);

	pack_time  (dump_job_ptr->start_time, buffer);
	pack_time  (dump_job_ptr->end_time, buffer);
	pack16  ((uint16_t) dump_job_ptr->job_state, buffer);
	pack16  ((uint16_t) dump_job_ptr->next_step_id, buffer);

	packstr (dump_job_ptr->nodes, buffer);
	packstr (dump_job_ptr->partition, buffer); 
	packstr (dump_job_ptr->name, buffer);

	/* Dump job details, if available */
	detail_ptr = dump_job_ptr->details;
	if (detail_ptr) {
		if (detail_ptr->magic != DETAILS_MAGIC)
			fatal ("dump_all_job: job detail integrity is bad");
		pack16  ((uint16_t) DETAILS_FLAG, buffer);
		dump_job_details_state (detail_ptr, buffer); 
	}
	else
		pack16  ((uint16_t) 0, buffer);	/* no details flag */

	/* Dump job steps */
	step_record_iterator = list_iterator_create (dump_job_ptr->step_list);		
	while ((step_record_ptr = (struct step_record *) list_next (step_record_iterator))) {
		pack16  ((uint16_t) STEP_FLAG, buffer);
		dump_job_step_state (step_record_ptr, buffer);
	};
	list_iterator_destroy (step_record_iterator);
	pack16  ((uint16_t) 0, buffer);	/* no step flag */
}

/*
 * dump_job_details_state - dump the state of a specific job details to a buffer
 * detail_ptr (I) - pointer to job details for which information is requested
 * buffer (I/O) - location to store data, pointers automatically advanced
 */
void 
dump_job_details_state (struct job_details *detail_ptr, Buf buffer) 
{
	char tmp_str[MAX_STR_PACK];

	pack_job_credential ( &detail_ptr->credential , buffer ) ;

	pack32  ((uint32_t) detail_ptr->num_procs, buffer);
	pack32  ((uint32_t) detail_ptr->num_nodes, buffer);

	pack16  ((uint16_t) detail_ptr->shared, buffer);
	pack16  ((uint16_t) detail_ptr->contiguous, buffer);
	pack16  ((uint16_t) detail_ptr->kill_on_node_fail, buffer);
	pack16  ((uint16_t) detail_ptr->batch_flag, buffer);

	pack32  ((uint32_t) detail_ptr->min_procs, buffer);
	pack32  ((uint32_t) detail_ptr->min_memory, buffer);
	pack32  ((uint32_t) detail_ptr->min_tmp_disk, buffer);
	pack_time  (detail_ptr->submit_time, buffer);
	pack32  ((uint32_t) detail_ptr->total_procs, buffer);

	if ((detail_ptr->req_nodes == NULL) ||
	    (strlen (detail_ptr->req_nodes) < MAX_STR_PACK))
		packstr (detail_ptr->req_nodes, buffer);
	else {
		strncpy(tmp_str, detail_ptr->req_nodes, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK-1] = (char) NULL;
		packstr (tmp_str, buffer);
	}

	if (detail_ptr->features == NULL ||
			strlen (detail_ptr->features) < MAX_STR_PACK)
		packstr (detail_ptr->features, buffer);
	else {
		strncpy(tmp_str, detail_ptr->features, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK-1] = (char) NULL;
		packstr (tmp_str, buffer);
	}

	if (detail_ptr->stderr == NULL ||
			strlen (detail_ptr->stderr) < MAX_STR_PACK)
		packstr (detail_ptr->stderr, buffer);
	else {
		strncpy(tmp_str, detail_ptr->stderr, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK-1] = (char) NULL;
		packstr (tmp_str, buffer);
	}

	if (detail_ptr->stdin == NULL ||
			strlen (detail_ptr->stdin) < MAX_STR_PACK)
		packstr (detail_ptr->stdin, buffer);
	else {
		strncpy(tmp_str, detail_ptr->stdin, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK-1] = (char) NULL;
		packstr (tmp_str, buffer);
	}

	if (detail_ptr->stdout == NULL ||
			strlen (detail_ptr->stdout) < MAX_STR_PACK)
		packstr (detail_ptr->stdout, buffer);
	else {
		strncpy(tmp_str, detail_ptr->stdout, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK-1] = (char) NULL;
		packstr (tmp_str, buffer);
	}

	if (detail_ptr->work_dir == NULL ||
			strlen (detail_ptr->work_dir) < MAX_STR_PACK)
		packstr (detail_ptr->work_dir, buffer);
	else {
		strncpy(tmp_str, detail_ptr->work_dir, MAX_STR_PACK);
		tmp_str[MAX_STR_PACK-1] = (char) NULL;
		packstr (tmp_str, buffer);
	}
}

/*
 * dump_job_step_state - dump the state of a specific job step to a buffer
 * detail_ptr (I) - pointer to job step for which information is requested
 * buffer (I/O) - location to store data, pointers automatically advanced
 */
void 
dump_job_step_state (struct step_record *step_ptr, Buf buffer) 
{
	char *node_list;

	pack16  ((uint16_t) step_ptr->step_id, buffer);
	pack16  ((uint16_t) step_ptr->cyclic_alloc, buffer);
	pack_time  (step_ptr->start_time, buffer);
	node_list = bitmap2node_name (step_ptr->node_bitmap);
	packstr (node_list, buffer);
	xfree (node_list);
#ifdef HAVE_LIBELAN3
	qsw_pack_jobinfo (step_ptr->qsw_job, buffer);
#endif
}

/*
 * load_job_state - load the job state from file, recover from slurmctld restart.
 *	execute this after loading the configuration file data.
 */
int
load_job_state ( void )
{
	int data_allocated, data_read = 0, error_code = 0;
	uint32_t time, data_size = 0;
	int state_fd;
	char *data, *state_file;
	Buf buffer;
	uint32_t job_id, user_id, time_limit, priority, total_procs;
	time_t start_time, end_time;
	uint16_t job_state, next_step_id, details;
	char *nodes = NULL, *partition = NULL, *name = NULL;
	uint32_t num_procs, num_nodes, min_procs, min_memory, min_tmp_disk, submit_time;
	uint16_t shared, contiguous, kill_on_node_fail, name_len, batch_flag;
	char *req_nodes = NULL, *features = NULL;
	char  *stderr = NULL, *stdin = NULL, *stdout = NULL, *work_dir = NULL;
	slurm_job_credential_t *credential_ptr = NULL;
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	bitstr_t *node_bitmap = NULL, *req_node_bitmap = NULL;
	uint16_t step_flag;

	/* read the file */
	state_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (state_file, "/job_state");
	lock_state_files ();
	state_fd = open (state_file, O_RDONLY);
	if (state_fd < 0) {
		info ("No job state file (%s) to recover", state_file);
		error_code = ENOENT;
	}
	else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while ((data_read = read (state_fd, &data[data_size], BUF_SIZE)) == BUF_SIZE) {
			data_size += data_read;
			data_allocated += BUF_SIZE;
			xrealloc(data, data_allocated);
		}
		data_size += data_read;
		close (state_fd);
		if (data_read < 0) 
			error ("Error reading file %s: %m", state_file);
	}
	xfree (state_file);
	unlock_state_files ();

	if (job_id_sequence < 0)
		job_id_sequence = slurmctld_conf . first_job_id;

	buffer = create_buf (data, data_size);
	if (data_size > sizeof (time_t))
		unpack_time (&time, buffer);

	while (remaining_buf (buffer) > 0) {
		safe_unpack32 (&job_id, buffer);
		safe_unpack32 (&user_id, buffer);
		safe_unpack32 (&time_limit, buffer);
		safe_unpack32 (&priority, buffer);

		unpack_time (&start_time, buffer);
		unpack_time (&end_time, buffer);
		safe_unpack16 (&job_state, buffer);
		safe_unpack16 (&next_step_id, buffer);

		safe_unpackstr_xmalloc (&nodes, &name_len, buffer);
		safe_unpackstr_xmalloc (&partition, &name_len, buffer);
		safe_unpackstr_xmalloc (&name, &name_len, buffer);

		safe_unpack16 (&details, buffer);
		if ((remaining_buf (buffer) < (11 * sizeof (uint32_t))) && details) {
			/* no room for details */
			error ("job state file problem on job %u", job_id);
			goto cleanup;
		}

		if (details == DETAILS_FLAG ) {
			unpack_job_credential (&credential_ptr , buffer);

			safe_unpack32 (&num_procs, buffer);
			safe_unpack32 (&num_nodes, buffer);

			safe_unpack16 (&shared, buffer);
			safe_unpack16 (&contiguous, buffer);
			safe_unpack16 (&kill_on_node_fail, buffer);
			safe_unpack16 (&batch_flag, buffer);

			safe_unpack32 (&min_procs, buffer);
			safe_unpack32 (&min_memory, buffer);
			safe_unpack32 (&min_tmp_disk, buffer);
			safe_unpack_time (&submit_time, buffer);
			safe_unpack32 (&total_procs, buffer);

			safe_unpackstr_xmalloc (&req_nodes, &name_len, buffer);
			safe_unpackstr_xmalloc (&features, &name_len, buffer);
			safe_unpackstr_xmalloc (&stderr, &name_len, buffer);
			safe_unpackstr_xmalloc (&stdin, &name_len, buffer);
			safe_unpackstr_xmalloc (&stdout, &name_len, buffer);
			safe_unpackstr_xmalloc (&work_dir, &name_len, buffer);
		}

		if (nodes) {
			error_code = node_name2bitmap (nodes, &node_bitmap);
			if (error_code) {
				error ("load_job_state: invalid nodes (%s) for job_id %u",
					nodes, job_id);
				goto cleanup;
			}
		}
		if (req_nodes) {
			error_code = node_name2bitmap (req_nodes, &req_node_bitmap);
			if (error_code) {
				error ("load_job_state: invalid req_nodes (%s) for job_id %u",
					req_nodes, job_id);
				goto cleanup;
			}
		}
	
		job_ptr = find_job_record (job_id);
		if (job_ptr == NULL) {
			part_ptr = list_find_first (part_list, &list_find_part, partition);
			if (part_ptr == NULL) {
				info ("load_job_state: invalid partition (%s) for job_id %u",
					partition, job_id);
				error_code = EINVAL;
				goto cleanup;
			}
			job_ptr = create_job_record (&error_code);
			if ( error_code ) {
				error ("load_job_state: unable to create job entry for job_id %u",
					job_id);
				goto cleanup ;
			}
			job_ptr->job_id = job_id;
			strncpy (job_ptr->partition, partition, MAX_NAME_LEN);
			job_ptr->part_ptr = part_ptr;
			add_job_hash (job_ptr);
			info ("recovered job id %u", job_id);
		}

		job_ptr->user_id = user_id;
		job_ptr->time_limit = time_limit;
		job_ptr->priority = priority;
		job_ptr->start_time = start_time;
		job_ptr->end_time = end_time;
		job_ptr->job_state = job_state;
		job_ptr->next_step_id = next_step_id;
		strncpy (job_ptr->name, name, MAX_NAME_LEN);
		job_ptr->nodes = nodes; nodes = NULL;
		job_ptr->node_bitmap = node_bitmap; node_bitmap = NULL;
		build_node_details (job_ptr->node_bitmap, &job_ptr->num_cpu_groups,
			&job_ptr->cpus_per_node, &job_ptr->cpu_count_reps);

		if (default_prio >= priority)
			default_prio = priority - 1;
		if (job_id_sequence <= job_id)
			job_id_sequence = job_id + 1;

		if (details == DETAILS_FLAG ) {
			job_ptr->details->num_procs = num_procs;
			job_ptr->details->num_nodes = num_nodes;
			job_ptr->details->shared = shared;
			job_ptr->details->contiguous = contiguous;
			job_ptr->details->kill_on_node_fail = kill_on_node_fail;
			job_ptr->details->batch_flag = batch_flag;
			job_ptr->details->min_procs = min_procs;
			job_ptr->details->min_memory = min_memory;
			job_ptr->details->min_tmp_disk = min_tmp_disk;
			job_ptr->details->submit_time = submit_time;
			job_ptr->details->total_procs = total_procs;
			job_ptr->details->req_nodes = req_nodes; req_nodes = NULL;
			job_ptr->details->req_node_bitmap = req_node_bitmap; req_node_bitmap = NULL;
			job_ptr->details->features = features; features = NULL;
			job_ptr->details->stderr = stderr; stderr = NULL;
			job_ptr->details->stdin = stdin; stdin = NULL;
			job_ptr->details->stdout = stdout; stdout = NULL;
			job_ptr->details->work_dir = work_dir; work_dir = NULL;
			memcpy (&job_ptr->details->credential, credential_ptr, 
					sizeof (job_ptr->details->credential));
		}

		safe_unpack16 (&step_flag, buffer);
		while ((step_flag == STEP_FLAG) && 
		       (remaining_buf (buffer) > (2 * sizeof (uint32_t)))) {
			struct step_record *step_ptr;
			uint16_t step_id, cyclic_alloc;
			uint32_t start_time;
			char *node_list;

			safe_unpack16 (&step_id, buffer);
			safe_unpack16 (&cyclic_alloc, buffer);
			unpack_time (&start_time, buffer);
			safe_unpackstr_xmalloc (&node_list, &name_len, buffer);

			step_ptr = create_step_record (job_ptr);
			if (step_ptr == NULL) 
				break;
			step_ptr->step_id = step_id;
			step_ptr->cyclic_alloc = cyclic_alloc;
			step_ptr->start_time = start_time;
			info ("recovered job step %u.%u", job_id, step_id);
			if (node_list) {
				(void) node_name2bitmap (node_list, &(step_ptr->node_bitmap));
				xfree (node_list);
			}
#ifdef HAVE_LIBELAN3
			if (remaining_buf (buffer) < QSW_PACK_SIZE)
				break;
			qsw_alloc_jobinfo(&step_ptr->qsw_job);
			qsw_unpack_jobinfo(step_ptr->qsw_job, buffer);
#endif
			safe_unpack16 (&step_flag, buffer);
		}

cleanup:
		if (name) {
			xfree (name);
			name = NULL; 
		}
		if (nodes) {
			xfree (nodes); 
			nodes = NULL; 
		}
		if (partition) {
			xfree (partition); 
			partition = NULL;
		}
		if (node_bitmap) {
			bit_free (node_bitmap); 
			node_bitmap = NULL; 
		}
		if (req_nodes) {
			xfree (req_nodes); 
			req_nodes = NULL; 
		}
		if (req_node_bitmap) {
			bit_free (req_node_bitmap); 
			req_node_bitmap = NULL; 
		}
		if (features) {
			xfree (features); 
			features = NULL; 
		}
		if (stderr) {
			xfree (stderr); 
			stderr = NULL; 
		}
		if (stdin) {
			xfree (stdin); 
			stdin = NULL; 
		}
		if (stdout) {
			xfree (stdout);	
			stdout = NULL; 
		}
		if (work_dir) {
			xfree (work_dir); 
			work_dir = NULL; 
		}
		if (credential_ptr) {
			xfree (credential_ptr); 
			credential_ptr = NULL; 
		}
	}

	free_buf (buffer);
	return error_code;
}

/* add_job_hash - add a job hash entry for given job record, job_id must already be set */
void 	
add_job_hash (struct job_record *job_ptr) 
{
	int inx;

	inx = job_hash_inx (job_ptr->job_id);
	if (job_hash[inx]) {
		if (max_hash_over >= MAX_JOB_COUNT)
			fatal ("Job hash table overflow");
		job_hash_over[max_hash_over++] = job_ptr;
	}
	else
		job_hash[inx] = job_ptr;
}


/* 
 * find_job_record - return a pointer to the job record with the given job_id
 * input: job_id - requested job's id
 * output: pointer to the job's record, NULL on error
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 * global: job_list - global job list pointer
 */
struct job_record *
find_job_record(uint32_t job_id) 
{
	int i;

	/* First try to find via hash table */
	if (job_hash[job_hash_inx (job_id)] &&
			job_hash[job_hash_inx (job_id)]->job_id == job_id)
		return job_hash[job_hash_inx (job_id)];

	/* linear search of overflow hash table overflow */
	for (i=0; i<max_hash_over; i++) {
		if (job_hash_over[i] != NULL &&
				job_hash_over[i]->job_id == job_id)
			return job_hash_over[i];
	}

	return NULL;
}

/* find_running_job_by_node_name - Given a node name, return a pointer to any 
 *	job currently running on that node */
struct job_record *
find_running_job_by_node_name (char *node_name)
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	struct node_record *node_record_point;
	int bit_position;

	node_record_point = find_node_record (node_name);
	if (node_record_point == NULL)	/* No such node */
		return NULL;
	bit_position = node_record_point - node_record_table_ptr;

	job_record_iterator = list_iterator_create (job_list);		
	while ((job_record_point = (struct job_record *) list_next (job_record_iterator))) {
		if ( (job_record_point->job_state != JOB_STAGE_IN) && 
		     (job_record_point->job_state != JOB_RUNNING) && 
		     (job_record_point->job_state != JOB_STAGE_OUT) )
			continue;	/* job not active */
		if (bit_test (job_record_point->node_bitmap, bit_position))
			break;		/* found job here */
	}		
	list_iterator_destroy (job_record_iterator);

	return job_record_point;
}

/* kill_running_job_by_node_name - Given a node name, deallocate that job 
 *	from the node or kill it 
 * returns: number of killed jobs
 */
int
kill_running_job_by_node_name (char *node_name)
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	struct node_record *node_record_point;
	int bit_position;
	int job_count = 0;

	node_record_point = find_node_record (node_name);
	if (node_record_point == NULL)	/* No such node */
		return 0;
	bit_position = node_record_point - node_record_table_ptr;

	job_record_iterator = list_iterator_create (job_list);		
	while ((job_record_point = (struct job_record *) list_next (job_record_iterator))) {
		if ( (job_record_point->job_state != JOB_STAGE_IN) && 
		     (job_record_point->job_state != JOB_RUNNING) && 
		     (job_record_point->job_state != JOB_STAGE_OUT) )
			continue;	/* job not active */
		if (bit_test (job_record_point->node_bitmap, bit_position) == 0)
			continue;	/* job not on this node */

		error ("Running job_id %u on failed node node %s",
			job_record_point->job_id, node_name);
		job_count++;
		if ( (job_record_point->details == NULL) || 
		     (job_record_point->details->kill_on_node_fail)) {
			last_job_update = time (NULL);
			job_record_point->job_state = JOB_NODE_FAIL;
			job_record_point->end_time = time(NULL);
			deallocate_nodes (job_record_point);
			delete_job_details(job_record_point);
		}

	}		
	list_iterator_destroy (job_record_iterator);

	return job_count;
}



/* dump_job_desc - dump the incoming job submit request message */
void
dump_job_desc(job_desc_msg_t * job_specs)
{
	long job_id, min_procs, min_memory, min_tmp_disk, num_procs;
	long num_nodes, time_limit, priority, contiguous;
	long kill_on_node_fail, shared;

	if (job_specs == NULL) 
		return;

	job_id = (job_specs->job_id != NO_VAL) ? job_specs->job_id : -1 ;
	debug3("JobDesc: user_id=%u job_id=%ld partition=%s name=%s\n", 
		job_specs->user_id, job_id, 
		job_specs->partition, job_specs->name);

	min_procs = (job_specs->min_procs != NO_VAL) ? job_specs->min_procs : -1 ;
	min_memory = (job_specs->min_memory != NO_VAL) ? job_specs->min_memory : -1 ;
	min_tmp_disk = (job_specs->min_tmp_disk != NO_VAL) ? job_specs->min_tmp_disk : -1 ;
	debug3("   min_procs=%ld min_memory=%ld min_tmp_disk=%ld features=%s", 
		min_procs, min_memory, min_tmp_disk, job_specs->features);

	num_procs = (job_specs->num_procs != NO_VAL) ? job_specs->num_procs : -1 ;
	num_nodes = (job_specs->num_nodes != NO_VAL) ? job_specs->num_nodes : -1 ;
	debug3("   num_procs=%ld num_nodes=%ld req_nodes=%s", 
		num_procs, num_nodes, job_specs->req_nodes);

	time_limit = (job_specs->time_limit != NO_VAL) ? job_specs->time_limit : -1 ;
	priority = (job_specs->priority != NO_VAL) ? job_specs->priority : -1 ;
	contiguous = (job_specs->contiguous != (uint16_t) NO_VAL) ? 
			job_specs->contiguous : -1 ;
	kill_on_node_fail = (job_specs->kill_on_node_fail != (uint16_t) NO_VAL) ? 
			job_specs->kill_on_node_fail : -1 ;
	shared = (job_specs->shared != (uint16_t) NO_VAL) ? job_specs->shared : -1 ;
	debug3("   time_limit=%ld priority=%ld contiguous=%ld shared=%ld", 
		time_limit, priority, contiguous, shared);

	debug3("   kill_on_node_fail=%ld script=%.40s...", 
		kill_on_node_fail, job_specs->script);

	if (job_specs->env_size == 1)
		debug3("   environment=\"%s\"", job_specs->environment[0]);
	else if (job_specs->env_size == 2)
		debug3("   environment=%s,%s", 
			job_specs->environment[0], job_specs->environment[1]);
	else if (job_specs->env_size > 2)
		debug3("   environment=%s,%s,%s,...", 
			job_specs->environment[0], job_specs->environment[1],
			job_specs->environment[2]);

	debug3("   stdin=%s stdout=%s stderr=%s work_dir=%s", 
		job_specs->stdin, job_specs->stdout, job_specs->stderr, 
		job_specs->work_dir);

}


/* 
 * init_job_conf - initialize the job configuration tables and values. 
 *	this should be called after creating node information, but 
 *	before creating any job entries.
 * output: return value - 0 if no error, otherwise an error code
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
int 
init_job_conf () 
{
	if (job_list == NULL) {
		job_count = 0;
		job_list = list_create (&list_delete_job);
		if (job_list == NULL)
			fatal ("init_job_conf: list_create can not allocate memory");
	}
	last_job_update = time (NULL);
	return SLURM_SUCCESS;
}


/*
 * job_allocate - create job_records for the suppied job specification and allocate nodes for it.
 * input: job_specs - job specifications
 *	new_job_id - location for storing new job's id
 *	node_list - location for storing new job's allocated nodes
 *	num_cpu_groups - location to store number of cpu groups
 *	cpus_per_node - location to store pointer to array of numbers of cpus on each node allocated
 *	cpu_count_reps - location to store pointer to array of numbers of consecutive nodes having
 *				 same cpu count
 *	immediate - if set then either initiate the job immediately or fail
 *	will_run - don't initiate the job if set, just test if it could run now or later
 *	allocate - resource allocation request if set, not a full job
 * output: new_job_id - the job's ID
 *	num_cpu_groups - number of cpu groups (elements in cpus_per_node and cpu_count_reps)
 *	cpus_per_node - pointer to array of numbers of cpus on each node allocate
 *	cpu_count_reps - pointer to array of numbers of consecutive nodes having same cpu count
 *	node_list - list of nodes allocated to the job
 *	returns 0 on success, EINVAL if specification is invalid, 
 *		EAGAIN if higher priority jobs exist
 * NOTE: If allocating nodes lx[0-7] to a job and those nodes have cpu counts of 
 *	 4, 4, 4, 4, 8, 8, 4, 4 then num_cpu_groups=3, cpus_per_node={4,8,4} and
 *	cpu_count_reps={4,2,2}
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 */

int
immediate_job_launch (job_desc_msg_t * job_specs, uint32_t *new_job_id, char **node_list, 
		uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps, 
		int immediate , int will_run, uid_t submit_uid )
{
	return job_allocate (job_specs, new_job_id, node_list, 
				num_cpu_groups, cpus_per_node, cpu_count_reps, 
				true , false , true, submit_uid );
}

int 
will_job_run (job_desc_msg_t * job_specs, uint32_t *new_job_id, char **node_list, 
		uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps, 
		int immediate , int will_run, uid_t submit_uid )
{
	return job_allocate (job_specs, new_job_id, node_list, 
				num_cpu_groups, cpus_per_node, cpu_count_reps, 
				false , true , true, submit_uid );
}

int 
job_allocate (job_desc_msg_t  *job_specs, uint32_t *new_job_id, char **node_list, 
	uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps, 
	int immediate, int will_run, int allocate, uid_t submit_uid)
{
	int error_code, test_only;
	struct job_record *job_ptr;

	error_code = job_create (job_specs, new_job_id, allocate, will_run, 
		&job_ptr, submit_uid);
	if (error_code)
		return error_code;
	if (job_ptr == NULL)
		fatal ("job_allocate: allocated job %u lacks record", new_job_id);

	if (immediate && top_priority(job_ptr) != 1) {
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time  = 0;
		return ESLURM_NOT_TOP_PRIORITY; 
	}

	test_only = will_run || (allocate == 0);
	if (test_only == 0) {
		/* Some of these pointers are NULL on submit (e.g. allocate == 0) */
		*num_cpu_groups = 0;
		node_list[0] = NULL;
		cpus_per_node[0] = cpu_count_reps[0] = NULL;
		last_job_update = time (NULL);
	}

	error_code = select_nodes(job_ptr, test_only);
	if (error_code == ESLURM_NODES_BUSY) {
		if (immediate) {
			job_ptr->job_state = JOB_FAILED;
			job_ptr->end_time  = 0;
		}
		else 	/* job remains queued */
			error_code = 0;
		return error_code;
	}

	if (error_code) {	/* fundamental flaw in job request */
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time  = 0;
		return error_code; 
	}

	if (will_run) {			/* job would run now, flag job record destruction */
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time  = 0;
	}

	if (test_only == 0) {
		node_list[0]      = job_ptr->nodes;
		*num_cpu_groups   = job_ptr->num_cpu_groups;
		cpus_per_node[0]  = job_ptr->cpus_per_node;
		cpu_count_reps[0] = job_ptr->cpu_count_reps;
	}
	return SLURM_SUCCESS;
}


/* 
 * job_cancel - cancel the specified job
 * input: job_id - id of the job to be cancelled
 *	uid - uid of requesting user
 * output: returns 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int
job_cancel (uint32_t job_id, uid_t uid) 
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info ("job_cancel: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->job_state == JOB_FAILED) ||
	    (job_ptr->job_state == JOB_COMPLETE) ||
	    (job_ptr->job_state == JOB_TIMEOUT))
		return ESLURM_ALREADY_DONE;

	if ( (job_ptr->user_id != uid) && 
	     (uid != 0) && (uid != getuid ()) ) {
		error ("Security violation, JOB_CANCEL RPC from uid %d", uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (job_ptr->job_state == JOB_PENDING) {
		last_job_update = time (NULL);
		job_ptr->job_state = JOB_FAILED;
		job_ptr->start_time = job_ptr->end_time = time(NULL);
		delete_job_details(job_ptr);
		verbose ("job_cancel of pending job %u successful", job_id);
		return SLURM_SUCCESS;
	}

	if ((job_ptr->job_state == JOB_STAGE_IN) || 
	    (job_ptr->job_state == JOB_RUNNING) ||
	    (job_ptr->job_state == JOB_STAGE_OUT)) {
		last_job_update = time (NULL);
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time = time(NULL);
		deallocate_nodes (job_ptr);
		delete_job_details(job_ptr);
		verbose ("job_cancel of running job %u successful", job_id);
		return SLURM_SUCCESS;
	} 

	verbose ("job_cancel: job %u can't be cancelled from state=%s", 
			job_id, job_state_string(job_ptr->job_state));
	return ESLURM_TRANSITION_STATE_NO_UPDATE;
}

/* 
 * job_complete - note the normal termination the specified job
 * input: job_id - id of the job which completed
 *	uid - user id of user issuing the RPC
 * output: returns 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int
job_complete (uint32_t job_id, uid_t uid) 
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info ("job_complete: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->job_state == JOB_FAILED) ||
	    (job_ptr->job_state == JOB_COMPLETE) ||
	    (job_ptr->job_state == JOB_TIMEOUT))
		return ESLURM_ALREADY_DONE;

	if ( (job_ptr->user_id != uid) &&
	     (uid != 0) && (uid != getuid ()) ) {
		error ("Security violation, JOB_COMPLETE RPC from uid %d", uid);
		return ESLURM_USER_ID_MISSING;
	}

	if ((job_ptr->job_state == JOB_STAGE_IN) || 
	    (job_ptr->job_state == JOB_RUNNING) ||
	    (job_ptr->job_state == JOB_STAGE_OUT)) {
		deallocate_nodes (job_ptr);
		verbose ("job_complete for job id %u successful", job_id);
	} 
	else {
		error ("job_complete for job id %u from bad state", job_id, job_ptr->job_state);
	}

	last_job_update = time (NULL);
	job_ptr->job_state = JOB_COMPLETE;
	job_ptr->end_time = time(NULL);
	delete_job_details(job_ptr);
	delete_all_step_records(job_ptr);
	return SLURM_SUCCESS;
}

/*
 * job_create - create a job table record for the supplied specifications.
 *	this performs only basic tests for request validity (access to partition, 
 *	nodes count in partition, and sufficient processors in partition).
 * input: job_specs - job specifications
 *	new_job_id - location for storing new job's id
 *	allocate - resource allocation request if set rather than job submit
 *	will_run - job is not to be created, test of validity only
 *	job_rec_ptr - place to park pointer to the job (or NULL)
 * output: new_job_id - the job's ID
 *	returns 0 on success, otherwise ESLURM error code
 *	allocate - if set, job allocation only (no script required)
 *	will_run - if set then test only, don't create a job entry
 *	job_rec_ptr - pointer to the job (if not passed a NULL)
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */

int
job_create ( job_desc_msg_t *job_desc, uint32_t *new_job_id, int allocate, 
		int will_run, struct job_record **job_rec_ptr, uid_t submit_uid )
{
	int error_code, i; 
	struct part_record *part_ptr;
	bitstr_t *req_bitmap = NULL ;

	if ( (error_code = validate_job_desc ( job_desc , allocate ) ) )
		return error_code;

	/* find selected partition */
	if (job_desc->partition) {
		part_ptr = list_find_first (part_list, &list_find_part,
				job_desc->partition);
		if (part_ptr == NULL) {
			info ("job_create: invalid partition specified: %s",
					job_desc->partition);
			error_code = ESLURM_INVALID_PARTITION_NAME;
			return error_code ;
		}		
	}
	else {
		if (default_part_loc == NULL) {
			error ("job_create: default partition not set.");
			error_code = ESLURM_DEFAULT_PARTITION_NOT_SET;
			return error_code ;
		}		
		part_ptr = default_part_loc;
	}
	if (job_desc->time_limit == NO_VAL)	/* Default time_limit is partition maximum */
		job_desc->time_limit = part_ptr->max_time;


	/* can this user access this partition */
	if ( (part_ptr->root_only) && (submit_uid != 0) ) {
		error ("job_create: non-root job submission to partition %s by uid %u", 
			part_ptr->name, (unsigned int) submit_uid);
		error_code = ESLURM_ACCESS_DENIED ;
		return error_code;
	}			
	if (validate_group (part_ptr, submit_uid) == 0) {
		info ("job_create: job lacks group required of partition %s, uid %u",
				part_ptr->name, (unsigned int) submit_uid);
		error_code = ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP;
		return error_code;
	}

	/* check if select partition has sufficient resources to satisfy request */
	if (job_desc->req_nodes) {	/* insure that selected nodes are in this partition */
		error_code = node_name2bitmap (job_desc->req_nodes, &req_bitmap);
		if (error_code == EINVAL)
			goto cleanup;
		if (error_code != 0) {
			error_code = EAGAIN;	/* no memory */
			goto cleanup;
		}		
		if (job_desc->contiguous)
			bit_fill_gaps (req_bitmap);
		if (bit_super_set (req_bitmap, part_ptr->node_bitmap) != 1) {
			info ("job_create: requested nodes %s not in partition %s",
					job_desc->req_nodes, part_ptr->name);
			error_code = ESLURM_REQUESTED_NODES_NOT_IN_PARTITION;
			goto cleanup;
		}		
		i = count_cpus (req_bitmap);
		if (i > job_desc->num_procs)
			job_desc->num_procs = i;
		i = bit_set_count (req_bitmap);
		if (i > job_desc->num_nodes)
			job_desc->num_nodes = i;
	}			
	if (job_desc->num_procs > part_ptr->total_cpus) {
		info ("job_create: too many cpus (%d) requested of partition %s(%d)",
				job_desc->num_procs, part_ptr->name, part_ptr->total_cpus);
		error_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
		goto cleanup;
	}			
	if ((job_desc->num_nodes > part_ptr->total_nodes) || 
			(job_desc->num_nodes > part_ptr->max_nodes)) {
		if (part_ptr->total_nodes > part_ptr->max_nodes)
			i = part_ptr->max_nodes;
		else
			i = part_ptr->total_nodes;
		info ("job_create: too many nodes (%d) requested of partition %s(%d)",
				job_desc->num_nodes, part_ptr->name, i);
		error_code = ESLURM_TOO_MANY_REQUESTED_NODES;
		goto cleanup;
	}

	/* Perform some size checks on strings we store to prevent malicious user */
	/* from filling slurmctld's memory */
	if (job_desc->stderr && (strlen (job_desc->stderr) > BUF_SIZE)) {
		info ("job_create: strlen(stderr) too big (%d)", strlen (job_desc->stderr));
		error_code = ESLURM_PATHNAME_TOO_LONG;
		goto cleanup;
	}
	if (job_desc->stdin && (strlen (job_desc->stdin) > BUF_SIZE)) {
		info ("job_create: strlen(stdin) too big (%d)", strlen (job_desc->stdin));
		error_code = ESLURM_PATHNAME_TOO_LONG;
		goto cleanup;
	}
	if (job_desc->stdout && (strlen (job_desc->stdout) > BUF_SIZE)) {
		info ("job_create: strlen(stdout) too big (%d)", strlen (job_desc->stdout));
		error_code = ESLURM_PATHNAME_TOO_LONG;
		goto cleanup;
	}
	if (job_desc->work_dir && (strlen (job_desc->work_dir) > BUF_SIZE)) {
		info ("job_create: strlen(work_dir) too big (%d)", strlen (job_desc->work_dir));
		error_code = ESLURM_PATHNAME_TOO_LONG;
		goto cleanup;
	}

	if (will_run) {
		error_code = 0;
		goto cleanup;
	}

	if ( ( error_code = copy_job_desc_to_job_record ( job_desc , job_rec_ptr , part_ptr , 
							req_bitmap ) ) )  {
		error_code = ESLURM_ERROR_ON_DESC_TO_RECORD_COPY ;
		goto cleanup ;
	}

	if (job_desc->script) {
		if ( ( error_code = copy_job_desc_to_file ( job_desc , (*job_rec_ptr)->job_id ) ) )  {
			error_code = ESLURM_WRITING_TO_FILE ;
			goto cleanup ;
		}
		(*job_rec_ptr)->details->batch_flag = 1;
	}
	else
		(*job_rec_ptr)->details->batch_flag = 0;

	if (part_ptr->shared == SHARED_FORCE)		/* shared=force */
		(*job_rec_ptr)->details->shared = 1;
	else if (((*job_rec_ptr)->details->shared != 1) || 
	         (part_ptr->shared == SHARED_NO))	/* user or partition want no sharing */
		(*job_rec_ptr)->details->shared = 0;

	*new_job_id = (*job_rec_ptr)->job_id;
	return SLURM_SUCCESS ;

	cleanup:
		if ( req_bitmap )
			bit_free (req_bitmap);
		return error_code ;
}

/* copy_job_desc_to_file - copy the job script and environment from the RPC structure 
 *	into a file */
int 
copy_job_desc_to_file ( job_desc_msg_t * job_desc , uint32_t job_id )
{
	int error_code = 0;
	char *dir_name, job_dir[20], *file_name;
	struct stat sbuf;

	/* Create state_save_location directory */
	dir_name = xstrdup (slurmctld_conf . state_save_location);

	/* Create job_id specific directory */
	sprintf (job_dir, "/job.%d", job_id);
	xstrcat (dir_name, job_dir);
	if (stat (dir_name, &sbuf) == -1) {	/* create job specific directory as needed */
		if (mkdir2 (dir_name, 0700))
			error ("mkdir2 on %s error %m", dir_name);
	}

	/* Create environment file, and write data to it */
	file_name = xstrdup (dir_name);
	xstrcat (file_name, "/environment");
	error_code = write_data_array_to_file (file_name, job_desc->environment, job_desc->env_size);
	xfree (file_name);

	/* Create script file */
	file_name = xstrdup (dir_name);
	xstrcat (file_name, "/script");
	error_code = write_data_to_file (file_name, job_desc->script);
	xfree (file_name);

	xfree (dir_name);
	return error_code;
}

/* mkdir2 - create a directory, does system call if root, runs mkdir otherwise */
int 
mkdir2 (char * path, int modes) 
{
	char *cmd;
	int error_code;

	if (getuid() == 0) {
		if (mknod (path, S_IFDIR | modes, 0))
			return errno;
	}

	else {
		cmd = xstrdup ("/bin/mkdir ");
		xstrcat (cmd, path);
		error_code = system (cmd);
		xfree (cmd);
		if (error_code)
			return error_code;
		(void) chmod (path, modes);
	}

	return SLURM_SUCCESS;
}

/* rmdir2 - Remove a directory, does system call if root, runs rmdir otherwise */
int 
rmdir2 (char * path) 
{
	char *cmd;
	int error_code;

	if (getuid() == 0) {
		if (unlink (path))
			return errno;
	}

	else {
		cmd = xstrdup ("/bin/rmdir ");
		xstrcat (cmd, path);
		error_code = system (cmd);
		xfree (cmd);
		if (error_code)
			return error_code;
	}

	return SLURM_SUCCESS;
}

/* Create file with specified name and write the supplied data array to it */
int
write_data_array_to_file ( char * file_name, char ** data, uint16_t size ) 
{
	int fd, i, pos, nwrite, amount;

	if (data == NULL) {
		(void) unlink (file_name);
		return SLURM_SUCCESS;
	}

	fd = creat (file_name, 0600);
	if (fd < 0) {
		error ("Error creating file %s, %m", file_name);
		return ESLURM_WRITING_TO_FILE;
	}

	amount = write (fd, &size, sizeof (uint16_t));
	if (amount < sizeof (uint16_t)) {
		error ("Error writing file %s, %m", file_name);
		close (fd);
		return ESLURM_WRITING_TO_FILE;
	}

	for (i = 0; i < size; i++) {
		nwrite = strlen(data[i]) + 1;
		pos = 0;
		while (nwrite > 0) {
			amount = write (fd, &data[i][pos], nwrite);
			if (amount < 0) {
				error ("Error writing file %s, %m", file_name);
				close (fd);
				return ESLURM_WRITING_TO_FILE;
			}
			nwrite -= amount;
			pos += amount;
		}
	}

	close (fd);
	return SLURM_SUCCESS;
}

/* Create file with specified name and write the supplied data to it */
int
write_data_to_file ( char * file_name, char * data ) 
{
	int fd, pos, nwrite, amount;

	if (data == NULL) {
		(void) unlink (file_name);
		return SLURM_SUCCESS;
	}

	fd = creat (file_name, 0600);
	if (fd < 0) {
		error ("Error creating file %s, %m", file_name);
		return ESLURM_WRITING_TO_FILE;
	}

	nwrite = strlen(data) + 1;
	pos = 0;
	while (nwrite > 0) {
		amount = write (fd, &data[pos], nwrite);
		if (amount < 0) {
			error ("Error writing file %s, %m", file_name);
			close (fd);
			return ESLURM_WRITING_TO_FILE;
		}
		nwrite -= amount;
		pos += amount;
	}
	close (fd);
	return SLURM_SUCCESS;
}

/* get_job_env - return the environment variables and their count for a given job */
char **
get_job_env (struct job_record *job_ptr, uint16_t *env_size)
{
	char job_dir[30], *file_name, **environment = NULL;

	file_name = xstrdup (slurmctld_conf . state_save_location);
	sprintf (job_dir, "/job.%d/environment", job_ptr->job_id);
	xstrcat (file_name, job_dir);

	read_data_array_from_file (file_name, &environment, env_size);

	xfree (file_name);
	return environment;
}

/* get_job_script - return the script for a given job */
char *
get_job_script (struct job_record *job_ptr)
{
	char job_dir[30], *file_name, *script = NULL;

	file_name = xstrdup (slurmctld_conf . state_save_location);
	sprintf (job_dir, "/job.%d/script", job_ptr->job_id);
	xstrcat (file_name, job_dir);

	read_data_from_file (file_name, &script);

	xfree (file_name);
	return script;
}

void
read_data_array_from_file ( char * file_name, char *** data, uint16_t *size )
{
	int fd, pos, buf_size, amount, i;
	char *buffer, **array_ptr;
	uint16_t rec_cnt;

	if ((file_name == NULL) || (data == NULL) || (size == NULL))
		fatal ("read_data_array_from_file passed NULL pointer");
	*data = NULL;
	*size = 0;

	fd = open (file_name, 0);
	if (fd < 0) {
		error ("Error opening file %s, %m", file_name);
		return;
	}

	amount = read (fd, &rec_cnt, sizeof (uint16_t));
	if (amount < sizeof (uint16_t)) {
		error ("Error reading file %s, %m", file_name);
		close (fd);
		return;
	}

	pos = 0;
	buf_size = 4096;
	buffer = xmalloc (buf_size);
	while (1) {
		amount = read (fd, &buffer[pos], buf_size);
		if (amount < 0) {
			error ("Error reading file %s, %m", file_name);
			xfree (buffer);
			close (fd);
			return;
		}
		if (amount < buf_size)	/* end of file */
			break;
		pos += amount;
		xrealloc (buffer, (pos+buf_size));
	}
	close (fd);

	/* We have all the data, now let's compute the pointers */
	pos = 0;
	array_ptr = xmalloc (rec_cnt * sizeof (char *));
	for (i=0; i<rec_cnt; i++) {
		array_ptr[i] = &buffer[pos];
		pos += strlen (&buffer[pos]) + 1;
		if ((pos > buf_size) && ((i+1) < rec_cnt)) {
			error ("Bad environment file %s", file_name);
			break;
		}
	}

	*size = rec_cnt;
	*data = array_ptr;
	return;
}

void
read_data_from_file ( char * file_name, char ** data)
{
	int fd, pos, buf_size, amount;
	char *buffer;

	if ((file_name == NULL) || (data == NULL))
		fatal ("read_data_from_file passed NULL pointer");
	*data = NULL;

	fd = open (file_name, 0);
	if (fd < 0) {
		error ("Error opening file %s, %m", file_name);
		return;
	}

	pos = 0;
	buf_size = 4096;
	buffer = xmalloc (buf_size);
	while (1) {
		amount = read (fd, &buffer[pos], buf_size);
		if (amount < 0) {
			error ("Error reading file %s, %m", file_name);
			xfree (buffer);
			close (fd);
			return;
		}
		if (amount < buf_size)	/* end of file */
			break;
		pos += amount;
		xrealloc (buffer, (pos+buf_size));
	}

	*data = buffer;
	close (fd);
	return;
}

/* copy_job_desc_to_job_record - copy the job descriptor from the RPC structure 
 *	into the actual slurmctld job record */
int 
copy_job_desc_to_job_record ( job_desc_msg_t * job_desc , 
	struct job_record ** job_rec_ptr , struct part_record *part_ptr, 
	bitstr_t *req_bitmap )
{
	int error_code ;
	struct job_details *detail_ptr;
	struct job_record *job_ptr ;

	job_ptr = create_job_record (&error_code);
	if ( error_code )
		return error_code ;

	strncpy (job_ptr->partition, part_ptr->name, MAX_NAME_LEN);
	job_ptr->part_ptr = part_ptr;
	if (job_desc->job_id != NO_VAL)
		job_ptr->job_id = job_desc->job_id;
	else
		set_job_id(job_ptr);
	add_job_hash (job_ptr);

	if (job_desc->name) {
		strncpy (job_ptr->name, job_desc->name , sizeof (job_ptr->name)) ;
	}
	job_ptr->user_id = (uid_t) job_desc->user_id;
	job_ptr->job_state = JOB_PENDING;
	job_ptr->time_limit = job_desc->time_limit;
	if ((job_desc->priority != NO_VAL) /* also check that submit UID is root */)
		job_ptr->priority = job_desc->priority;
	else
		set_job_prio (job_ptr);

	detail_ptr = job_ptr->details;
	detail_ptr->num_procs = job_desc->num_procs;
	detail_ptr->num_nodes = job_desc->num_nodes;
	if (job_desc->req_nodes) {
		detail_ptr->req_nodes = xstrdup ( job_desc->req_nodes );
		detail_ptr->req_node_bitmap = req_bitmap;
	}
	if (job_desc->features)
		detail_ptr->features = xstrdup ( job_desc->features );
	if (job_desc->shared != NO_VAL)
		detail_ptr->shared = job_desc->shared;
	if (job_desc->contiguous != NO_VAL)
		detail_ptr->contiguous = job_desc->contiguous;
	if (job_desc->kill_on_node_fail != NO_VAL)
		detail_ptr->kill_on_node_fail = job_desc->kill_on_node_fail;
	if (job_desc->min_procs != NO_VAL)
		detail_ptr->min_procs = job_desc->min_procs;
	if (job_desc->min_memory != NO_VAL)
		detail_ptr->min_memory = job_desc->min_memory;
	if (job_desc->min_tmp_disk != NO_VAL)
		detail_ptr->min_tmp_disk = job_desc->min_tmp_disk;
	if (job_desc->stderr)
		detail_ptr->stderr = xstrdup ( job_desc->stderr );
	if (job_desc->stdin)
		detail_ptr->stdin = xstrdup ( job_desc->stdin );
	if (job_desc->stdout)
		detail_ptr->stdout = xstrdup ( job_desc->stdout );
	if (job_desc->work_dir)
		detail_ptr->work_dir = xstrdup ( job_desc->work_dir );

	/* job_ptr->nodes		*leave as NULL pointer for now */
	/* job_ptr->start_time		*leave as NULL pointer for now */
	/* job_ptr->end_time		*leave as NULL pointer for now */
	/* detail_ptr->total_procs	*leave as NULL pointer for now */

	/* job credential */
	detail_ptr->credential . job_id = job_ptr->job_id ;
	detail_ptr->credential . user_id = job_ptr->user_id ;
	detail_ptr->credential . node_list = xstrdup ( job_ptr->nodes ) ;
	detail_ptr->credential . expiration_time = job_ptr->end_time;
	if ( sign_credential ( & sign_ctx , & detail_ptr->credential ) )
	{
		
	}

	*job_rec_ptr = job_ptr;
	return SLURM_SUCCESS;
}

/* 
 * job_step_cancel - cancel the specified job step
 * input: job_id, step_id - id of the job to be cancelled
 *	uid - user id of user issuing the RPC
 * output: returns 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int
job_step_cancel (uint32_t job_id, uint32_t step_id, uid_t uid) 
{
	struct job_record *job_ptr;
	int error_code;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {

		info ("job_step_cancel: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->job_state == JOB_FAILED) ||
	    (job_ptr->job_state == JOB_COMPLETE) ||
	    (job_ptr->job_state == JOB_TIMEOUT))
		return ESLURM_ALREADY_DONE;

	if ( (job_ptr->user_id != uid) && 
	     (uid != 0) && (uid != getuid ()) ) {
		error ("Security violation, JOB_CANCEL RPC from uid %d", uid);
		return ESLURM_USER_ID_MISSING;
	}

	if ((job_ptr->job_state == JOB_STAGE_IN) || 
	    (job_ptr->job_state == JOB_RUNNING) ||
	    (job_ptr->job_state == JOB_STAGE_OUT)) {
		last_job_update = time (NULL);
		error_code = delete_step_record (job_ptr, step_id);
		if (error_code == ENOENT) {
			info ("job_step_cancel step %u.%u not found", job_id, step_id);
			return ESLURM_ALREADY_DONE;
		}

		return SLURM_SUCCESS;
	} 

	info ("job_step_cancel: step %u.%u can't be cancelled from state=%s", 
			job_id, step_id, job_state_string(job_ptr->job_state));
	return ESLURM_TRANSITION_STATE_NO_UPDATE;

}

/* 
 * job_step_complete - note normal completion the specified job step
 * input: job_id, step_id - id of the job to be completed
 *	uid - user id of user issuing RPC
 * output: returns 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int
job_step_complete (uint32_t job_id, uint32_t step_id, uid_t uid) 
{
	struct job_record *job_ptr;
	int error_code;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info ("job_step_complete: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->job_state == JOB_FAILED) ||
	    (job_ptr->job_state == JOB_COMPLETE) ||
	    (job_ptr->job_state == JOB_TIMEOUT))
		return ESLURM_ALREADY_DONE;

	if ( (job_ptr->user_id != uid) && 
	     (uid != 0) && (uid != getuid ()) ) {
		error ("Security violation, JOB_COMPLETE RPC from uid %d", uid);
		return ESLURM_USER_ID_MISSING;
	}

	last_job_update = time (NULL);
	error_code = delete_step_record (job_ptr, step_id);
	if (error_code == ENOENT) {
		info ("job_step_complete step %u.%u not found", job_id, step_id);
		return ESLURM_ALREADY_DONE;
	}
	return SLURM_SUCCESS;
}

/* 
 * job_time_limit - terminate jobs which have exceeded their time limit
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
void
job_time_limit (void) 
{
	ListIterator job_record_iterator;
	struct job_record *job_ptr;
	time_t now;

	now = time (NULL);
	job_record_iterator = list_iterator_create (job_list);		
	while ((job_ptr = (struct job_record *) list_next (job_record_iterator))) {
		if (job_ptr->magic != JOB_MAGIC)
			fatal ("job_time_limit: job integrity is bad");
		if ((job_ptr->time_limit == INFINITE) ||
		    (job_ptr->end_time > now))
			continue;
		if ((job_ptr->job_state == JOB_PENDING) ||
		    (job_ptr->job_state == JOB_FAILED) ||
		    (job_ptr->job_state == JOB_COMPLETE) ||
		    (job_ptr->job_state == JOB_TIMEOUT) ||
		    (job_ptr->job_state == JOB_NODE_FAIL))
			continue;
		last_job_update = now;
		info ("Time limit exhausted for job_id %u, terminated", job_ptr->job_id);
		job_ptr->job_state = JOB_TIMEOUT;
		job_ptr->end_time = time(NULL);
		deallocate_nodes (job_ptr);
		delete_job_details(job_ptr);
	}		

	list_iterator_destroy (job_record_iterator);
}

/* validate_job_desc - validate that a job descriptor for job submit or 
 *	allocate has valid data, set values to defaults as required */
int 
validate_job_desc ( job_desc_msg_t * job_desc_msg , int allocate )
{
	if ((job_desc_msg->num_procs == NO_VAL) && (job_desc_msg->num_nodes == NO_VAL) && 
			(job_desc_msg->req_nodes == NULL)) {
		info ("job_create: job failed to specify ReqNodes, TotalNodes or TotalProcs");
		return ESLURM_JOB_MISSING_SIZE_SPECIFICATION;
	}
	if (allocate == SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 && 
	    job_desc_msg->script == NULL) {
		info ("job_create: job failed to specify Script");
		return ESLURM_JOB_SCRIPT_MISSING;
	}			
	if (job_desc_msg->user_id == NO_VAL) {
		info ("job_create: job failed to specify User");
		return ESLURM_USER_ID_MISSING;
	}	
	if (job_desc_msg->name && strlen(job_desc_msg->name) > MAX_NAME_LEN) {
		info ("job_create: job name %s too long", job_desc_msg->name);
		return ESLURM_JOB_NAME_TOO_LONG;
	}	
	if (job_desc_msg->contiguous == NO_VAL)
		job_desc_msg->contiguous = 0 ;
	if (job_desc_msg->kill_on_node_fail == NO_VAL)
		job_desc_msg->kill_on_node_fail = 1 ;
	if (job_desc_msg->shared == NO_VAL)
		job_desc_msg->shared =  0 ;

	if (job_desc_msg->job_id != NO_VAL && 
	    find_job_record ((uint32_t) job_desc_msg->job_id))
	{
		info  ("job_create: Duplicate job id %d", job_desc_msg->job_id);
		return ESLURM_DUPLICATE_JOB_ID;
	}
	if (job_desc_msg->num_procs == NO_VAL)
		job_desc_msg->num_procs = 1;		/* default cpu count of 1 */
	if (job_desc_msg->num_nodes == NO_VAL)
		job_desc_msg->num_nodes = 1;		/* default node count of 1 */
	if (job_desc_msg->min_memory == NO_VAL)
		job_desc_msg->min_memory = 1;		/* default is 1 MB memory per node */
	if (job_desc_msg->min_tmp_disk == NO_VAL)
		job_desc_msg->min_tmp_disk = 1;		/* default is 1 MB disk per node */
	if (job_desc_msg->shared == NO_VAL)
		job_desc_msg->shared = 0;		/* default is not shared nodes */
	if (job_desc_msg->min_procs == NO_VAL)
		job_desc_msg->min_procs = 1;		/* default is 1 processor per node */
	return SLURM_SUCCESS ;
}

/* 
 * list_delete_job - delete a job record and its corresponding job_details,
 *	see common/list.h for documentation
 * input: job_entry - pointer to job_record to delete
 * global: job_list - pointer to global job list
 *	job_count - count of job list entries
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */
void 
list_delete_job (void *job_entry)
{
	struct job_record *job_record_point;
	int i, j;

	job_record_point = (struct job_record *) job_entry;
	if (job_record_point == NULL)
		fatal ("list_delete_job: passed null job pointer");
	if (job_record_point->magic != JOB_MAGIC)
		fatal ("list_delete_job: passed invalid job pointer");

	if (job_hash[job_hash_inx (job_record_point->job_id)] == job_record_point)
		job_hash[job_hash_inx (job_record_point->job_id)] = NULL;
	else {
		for (i=0; i<max_hash_over; i++) {
			if (job_hash_over[i] != job_record_point)
				continue;
			for (j=i+1; j<max_hash_over; j++) {
				job_hash_over[j-1] = job_hash_over[j];
			}
			job_hash_over[--max_hash_over] = NULL;
			break;
		}
	}

	delete_job_details (job_record_point);

	if (job_record_point->nodes)
		xfree (job_record_point->nodes);
	if (job_record_point->node_bitmap)
		bit_free (job_record_point->node_bitmap);
	if (job_record_point->step_list) {
		delete_all_step_records (job_record_point);
		list_destroy (job_record_point->step_list);
	}
	job_count--;
	xfree(job_record_point);
}


/*
 * list_find_job_id - find an entry in the job list,  
 *	see common/list.h for documentation, key is the job's id 
 * global- job_list - the global partition list
 */
int 
list_find_job_id (void *job_entry, void *key) 
{
	if (((struct job_record *) job_entry)->job_id == *((uint32_t *) key))
		return 1;
	return SLURM_SUCCESS;
}


/*
 * list_find_job_old - find an entry in the job list,  
 *	see common/list.h for documentation, key is ignored 
 * global- job_list - the global partition list
 */
int 
list_find_job_old (void *job_entry, void *key) 
{
	time_t min_age;

	min_age = time(NULL) - MIN_JOB_AGE;

	if (((struct job_record *) job_entry)->end_time  >  min_age)
		return 0;

	if ((((struct job_record *) job_entry)->job_state != JOB_COMPLETE)  &&
	    (((struct job_record *) job_entry)->job_state != JOB_FAILED)  &&
	    (((struct job_record *) job_entry)->job_state != JOB_TIMEOUT))
		return 0;

	return 1;
}


/* 
 * pack_all_jobs - dump all job information for all jobs in 
 *	machine independent form (for network transmission)
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if job records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change unpack_job_desc() in common/slurm_protocol_pack.c whenever the
 *	data format changes
 */
void 
pack_all_jobs (char **buffer_ptr, int *buffer_size, time_t * update_time) 
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	uint32_t jobs_packed = 0, tmp_offset ;
	Buf buffer;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	if (*update_time == last_job_update)
		return;

	buffer = init_buf (BUF_SIZE*16);

	/* write message body header : size and time */
	/* put in a place holder job record count of 0 for now */
	pack32  ((uint32_t) jobs_packed, buffer);
	pack_time  (last_job_update, buffer);

	/* write individual job records */
	job_record_iterator = list_iterator_create (job_list);		
	while ((job_record_point = (struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("dump_all_job: job integrity is bad");

		pack_job (job_record_point, buffer);
		jobs_packed ++ ;
	}		

	list_iterator_destroy (job_record_iterator);

	/* put the real record count in the message body header */	
	tmp_offset = get_buf_offset (buffer);
	set_buf_offset (buffer, 0);
	pack32  ((uint32_t) jobs_packed, buffer);
	set_buf_offset (buffer, tmp_offset);

	*update_time = last_job_update;
	*buffer_size = get_buf_offset (buffer);
	buffer_ptr[0] = xfer_buf_data (buffer);
}


/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission)
 * dump_job_ptr (I) - pointer to job for which information is requested
 * buffer (I/O) - buffer in which data is place, pointers automatically updated
 * NOTE: change unpack_job_desc() in common/slurm_protocol_pack.c whenever the
 *	 data format changes
 */
void 
pack_job (struct job_record *dump_job_ptr, Buf buffer) 
{
	char tmp_str[MAX_STR_PACK];
	struct job_details *detail_ptr;

	pack32  (dump_job_ptr->job_id, buffer);
	pack32  (dump_job_ptr->user_id, buffer);
	pack16  ((uint16_t) dump_job_ptr->job_state, buffer);
	pack32  (dump_job_ptr->time_limit, buffer);

	pack_time  (dump_job_ptr->start_time, buffer);
	pack_time  (dump_job_ptr->end_time, buffer);
	pack32  (dump_job_ptr->priority, buffer);

	packstr (dump_job_ptr->nodes, buffer);
	packstr (dump_job_ptr->partition, buffer);
	packstr (dump_job_ptr->name, buffer);
	if (dump_job_ptr->node_bitmap) {
		(void) bit_fmt(tmp_str, MAX_STR_PACK, dump_job_ptr->node_bitmap);
		packstr (tmp_str, buffer);
	}
	else 
		packstr (NULL, buffer);

	detail_ptr = dump_job_ptr->details;
	if (detail_ptr && dump_job_ptr->job_state == JOB_PENDING) {
		if (detail_ptr->magic != DETAILS_MAGIC)
			fatal ("dump_all_job: job detail integrity is bad");
		pack32  ((uint32_t) detail_ptr->num_procs, buffer);
		pack32  ((uint32_t) detail_ptr->num_nodes, buffer);
		pack16  ((uint16_t) detail_ptr->shared, buffer);
		pack16  ((uint16_t) detail_ptr->contiguous, buffer);

		pack32  ((uint32_t) detail_ptr->min_procs, buffer);
		pack32  ((uint32_t) detail_ptr->min_memory, buffer);
		pack32  ((uint32_t) detail_ptr->min_tmp_disk, buffer);

		if ((detail_ptr->req_nodes == NULL) ||
		    (strlen (detail_ptr->req_nodes) < MAX_STR_PACK))
			packstr (detail_ptr->req_nodes, buffer);
		else {
			strncpy(tmp_str, detail_ptr->req_nodes, MAX_STR_PACK);
			tmp_str[MAX_STR_PACK-1] = (char) NULL;
			packstr (tmp_str, buffer);
		}

		if (detail_ptr->req_node_bitmap) {
			(void) bit_fmt(tmp_str, MAX_STR_PACK, detail_ptr->req_node_bitmap);
			packstr (tmp_str, buffer);
		}
		else 
			packstr (NULL, buffer);

		if (detail_ptr->features == NULL ||
				strlen (detail_ptr->features) < MAX_STR_PACK)
			packstr (detail_ptr->features, buffer);
		else {
			strncpy(tmp_str, detail_ptr->features, MAX_STR_PACK);
			tmp_str[MAX_STR_PACK-1] = (char) NULL;
			packstr (tmp_str, buffer);
		}
	}
	else {
		pack32  ((uint32_t) 0, buffer);
		pack32  ((uint32_t) 0, buffer);
		pack16  ((uint16_t) 0, buffer);
		pack16  ((uint16_t) 0, buffer);

		pack32  ((uint32_t) 0, buffer);
		pack32  ((uint32_t) 0, buffer);
		pack32  ((uint32_t) 0, buffer);

		packstr (NULL, buffer);
		packstr (NULL, buffer);
		packstr (NULL, buffer);
	}
}

/*
 * purge_old_job - purge old job records. 
 *	the jobs must have completed at least MIN_JOB_AGE minutes ago
 * global: job_list - global job table
 *	last_job_update - time of last job table update
 */
void
purge_old_job (void) 
{
	int i;

	i = list_delete_all (job_list, &list_find_job_old, "");
	if (i) {
		info ("purge_old_job: purged %d old job records", i);
		last_job_update = time (NULL);
	}
}


/* 
 * reset_job_bitmaps - reestablish bitmaps for existing jobs. 
 *	this should be called after rebuilding node information, 
 *	but before using any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
void 
reset_job_bitmaps () 
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;

	if (job_list == NULL)
		fatal ("init_job_conf: list_create can not allocate memory");

	job_record_iterator = list_iterator_create (job_list);		
	while ((job_record_point = 
				(struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("dump_all_job: job integrity is bad");
		if (job_record_point->node_bitmap)
			bit_free(job_record_point->node_bitmap);
		if (job_record_point->nodes) {
			node_name2bitmap (job_record_point->nodes,
					&job_record_point->node_bitmap);
			if ( (job_record_point->job_state == JOB_STAGE_IN) ||
			     (job_record_point->job_state == JOB_RUNNING) ||
			     (job_record_point->job_state == JOB_STAGE_OUT) )
				allocate_nodes ( job_record_point->node_bitmap ) ;

		}

		if (job_record_point->details == NULL)
			continue;
		if (job_record_point->details->req_node_bitmap)
			bit_free(job_record_point->details->req_node_bitmap);
		if (job_record_point->details->req_nodes)
			node_name2bitmap (job_record_point->details->req_nodes,
					&job_record_point->details->req_node_bitmap);
	}

	list_iterator_destroy (job_record_iterator);
	last_job_update = time (NULL);
}


/*
 * set_job_id - set a default job_id, insure that it is unique
 * input: job_ptr - pointer to the job_record
 */
void
set_job_id (struct job_record *job_ptr)
{
	uint32_t new_id;

	if (job_id_sequence < 0)
		job_id_sequence = slurmctld_conf . first_job_id;

	if ((job_ptr == NULL) || (job_ptr->magic != JOB_MAGIC)) 
		fatal ("set_job_id: invalid job_ptr");
	if ((job_ptr->partition == NULL) || (strlen(job_ptr->partition) == 0))
		fatal ("set_job_id: partition not set");

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
 * set_job_prio - set a default job priority
 * input: job_ptr - pointer to the job_record
 * NOTE: this is a simple prototype, we need to re-establish value on restart
 */
void
set_job_prio (struct job_record *job_ptr)
{
	if ((job_ptr == NULL) || (job_ptr->magic != JOB_MAGIC)) 
		fatal ("set_job_prio: invalid job_ptr");
	job_ptr->priority = default_prio--;
}


/* 
 * top_priority - determine if any other job for this partition has a higher priority
 *	than specified job
 * input: job_ptr - pointer to selected partition
 * output: returns 1 if selected job has highest priority, 0 otherwise
 */
int
top_priority (struct job_record *job_ptr) {
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	int top;

	top = 1;	/* assume top priority until found otherwise */
	job_record_iterator = list_iterator_create (job_list);		
	while ((job_record_point = (struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("top_priority: job integrity is bad");
		if (job_record_point == job_ptr)
			continue;
		if (job_record_point->job_state != JOB_PENDING)
			continue;
		if (job_record_point->priority >  job_ptr->priority &&
				job_record_point->part_ptr == job_ptr->part_ptr) {
			top = 0;
			break;
		}
	}		

	list_iterator_destroy (job_record_iterator);
	return top;
}


/*
 * update_job - update a job's parameters per the supplied specifications
 * input: uid - uid of user issuing RPC
 * output: returns an error code from common/slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
int 
update_job (job_desc_msg_t * job_specs, uid_t uid) 
{
	int error_code = SLURM_SUCCESS;
	int super_user = 0;
	struct job_record *job_ptr;
	struct job_details *detail_ptr;
	struct part_record *tmp_part_ptr;
	bitstr_t *req_bitmap = NULL ;

	job_ptr = find_job_record (job_specs -> job_id);
	if (job_ptr == NULL) {
		error ("update_job: job_id %u does not exist.", job_specs -> job_id);
		return ESLURM_INVALID_JOB_ID;
	}
	if ( (uid == 0) || (uid == getuid ()) )
		super_user = 1;
	if ( (job_ptr->user_id != uid) && 
	     (super_user == 0) ) {
		error ("Security violation, JOB_UPDATE RPC from uid %d", uid);
		return ESLURM_USER_ID_MISSING;
	}

	detail_ptr = job_ptr->details;
	last_job_update = time (NULL);

	if (job_specs -> time_limit != NO_VAL) {
		if ( super_user || 
		     (job_ptr -> time_limit > job_specs -> time_limit) ) {
			job_ptr -> time_limit = job_specs -> time_limit;
			job_ptr -> end_time = job_ptr -> start_time + (job_ptr -> time_limit * 60);
			info ("update_job: setting time_limit to %u for job_id %u",
				job_specs -> time_limit, job_specs -> job_id);
		}
		else {
			error ("Attempt to increase time limit for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> priority != NO_VAL) {
		if ( super_user ||
		     (job_ptr -> priority > job_specs -> priority) ) {
			job_ptr -> priority = job_specs -> priority;
			info ("update_job: setting priority to %u for job_id %u",
				job_specs -> priority, job_specs -> job_id);
		}
		else {
			error ("Attempt to increase priority for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> min_procs != NO_VAL && detail_ptr) {
		if ( super_user ||
		     (detail_ptr -> min_procs > job_specs -> min_procs) ) {
			detail_ptr -> min_procs = job_specs -> min_procs;
			info ("update_job: setting min_procs to %u for job_id %u",
				job_specs -> min_procs, job_specs -> job_id);
		}
		else {
			error ("Attempt to increase min_procs for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> min_memory != NO_VAL && detail_ptr) {
		if ( super_user ||
		     (detail_ptr -> min_memory > job_specs -> min_memory) ) {
			detail_ptr -> min_memory = job_specs -> min_memory;
			info ("update_job: setting min_memory to %u for job_id %u",
				job_specs -> min_memory, job_specs -> job_id);
		}
		else {
			error ("Attempt to increase min_memory for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> min_tmp_disk != NO_VAL && detail_ptr) {
		if ( super_user ||
		     (detail_ptr -> min_tmp_disk > job_specs -> min_tmp_disk) ) {
			detail_ptr -> min_tmp_disk = job_specs -> min_tmp_disk;
			info ("update_job: setting min_tmp_disk to %u for job_id %u",
				job_specs -> min_tmp_disk, job_specs -> job_id);
		}
		else {
			error ("Attempt to increase min_tmp_disk for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> num_procs != NO_VAL && detail_ptr) {
		if ( super_user ||
		     (detail_ptr -> num_procs > job_specs -> num_procs) ) {
			detail_ptr -> num_procs = job_specs -> num_procs;
			info ("update_job: setting num_procs to %u for job_id %u",
				job_specs -> num_procs, job_specs -> job_id);
		}
		else {
			error ("Attempt to increase num_procs for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> num_nodes != NO_VAL && detail_ptr) {
		if ( super_user ||
		     (detail_ptr -> num_nodes > job_specs -> num_nodes) ) {
			detail_ptr -> num_nodes = job_specs -> num_nodes;
			info ("update_job: setting num_nodes to %u for job_id %u",
				job_specs -> num_nodes, job_specs -> job_id);
		}
		else {
			error ("Attempt to increase num_nodes for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> shared != (uint16_t) NO_VAL && detail_ptr) {
		if ( super_user ||
		     (detail_ptr -> shared > job_specs -> shared) ) {
			detail_ptr -> shared = job_specs -> shared;
			info ("update_job: setting shared to %u for job_id %u",
				job_specs -> shared, job_specs -> job_id);
		}
		else {
			error ("Attempt to remove sharing for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> contiguous != (uint16_t) NO_VAL && detail_ptr) {
		if ( super_user ||
		     (detail_ptr -> contiguous > job_specs -> contiguous) ) {
			detail_ptr -> contiguous = job_specs -> contiguous;
			info ("update_job: setting contiguous to %u for job_id %u",
				job_specs -> contiguous, job_specs -> job_id);
		}
		else {
			error ("Attempt to add contiguous for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> kill_on_node_fail != (uint16_t) NO_VAL && detail_ptr) {
		detail_ptr -> kill_on_node_fail = job_specs -> kill_on_node_fail;
		info ("update_job: setting kill_on_node_fail to %u for job_id %u",
			job_specs -> kill_on_node_fail, job_specs -> job_id);
	}

	if (job_specs -> features && detail_ptr) {
		if ( super_user ) {
			if (detail_ptr -> features)
				xfree (detail_ptr -> features);
			detail_ptr -> features = job_specs -> features;
			info ("update_job: setting features to %s for job_id %u",
				job_specs -> features, job_specs -> job_id);
			job_specs -> features = NULL;
		}
		else {
			error ("Attempt to change features for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> name) {
		strncpy(job_ptr -> name, job_specs -> name, MAX_NAME_LEN);
		info ("update_job: setting name to %s for job_id %u",
				job_specs -> name, job_specs -> job_id);
	}

	if (job_specs -> partition) {
		tmp_part_ptr = find_part_record (job_specs -> partition);
		if (tmp_part_ptr == NULL)
			error_code =  ESLURM_INVALID_PARTITION_NAME;
		if ( (super_user && tmp_part_ptr) ) {
			strncpy(job_ptr -> partition, job_specs -> partition, MAX_NAME_LEN);
			job_ptr -> part_ptr = tmp_part_ptr;
			info ("update_job: setting partition to %s for job_id %u",
				job_specs -> partition, job_specs -> job_id);
			job_specs -> partition = NULL;
		}
		else {
			error ("Attempt to change partition for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if (job_specs -> req_nodes && detail_ptr) {
		if (super_user){
			if (node_name2bitmap (job_specs->req_nodes, &req_bitmap)) {
				error ("Invalid node list specified for job_update: %s", 
					job_specs->req_nodes);
				if ( req_bitmap )
					bit_free (req_bitmap);
				req_bitmap = NULL;
				error_code = ESLURM_INVALID_NODE_NAME;
			}
			if (req_bitmap) {
				if (detail_ptr -> req_nodes)
					xfree (detail_ptr -> req_nodes);
				detail_ptr -> req_nodes = job_specs -> req_nodes;
				if (detail_ptr->req_node_bitmap)
					bit_free (detail_ptr->req_node_bitmap);
				detail_ptr->req_node_bitmap = req_bitmap;
				info ("update_job: setting req_nodes to %s for job_id %u",
					job_specs -> req_nodes, job_specs -> job_id);
				job_specs -> req_nodes = NULL;
			}
		}
		else {
			error ("Attempt to change req_nodes for job %u", job_specs -> job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	return error_code;
}


/* validate_jobs_on_node - validate that any jobs that should be on the node are 
 *	actually running, if not clean up the job records and/or node records,
 *	call this function after validate_node_specs() sets the node state properly */
void 
validate_jobs_on_node ( char *node_name, uint32_t job_count, 
			uint32_t *job_id_ptr, uint16_t *step_id_ptr)
{
	int i, node_inx;
	struct node_record *node_ptr;
	struct job_record *job_ptr;

	node_ptr = find_node_record (node_name);
	if (node_ptr == NULL) {
		error ("slurmd registered on unknown node %s", node_name);
		return;
	}
	node_inx = node_ptr - node_record_table_ptr;

	/* If no job is running here, ensure none are assigned to this node */
	if (job_count == 0) {
		 (void) kill_running_job_by_node_name (node_name);
		return;
	}

	/* Ensure that jobs which are running are really supposed to be there */
	for (i=0; i<job_count; i++) {
		job_ptr = find_job_record (job_id_ptr[i]);
		if (job_ptr == NULL) {
			/* FIXME: In the future try to let job run */
			error ("Orphan job_id %u reported on node %s", 
			       job_id_ptr[i], node_name);
			signal_job_on_node (job_id_ptr[i], step_id_ptr[i], 
						SIGKILL, node_name);
			/* We may well have pending purge job RPC to send slurmd,
			 * which would synchronize this */
		}

		else if ( (job_ptr->job_state == JOB_STAGE_IN) ||
		     (job_ptr->job_state == JOB_RUNNING) ||
		     (job_ptr->job_state == JOB_STAGE_OUT) ) {
			if ( bit_test (job_ptr->node_bitmap, node_inx))
				debug3 ("Registered job_id %u on node %s ", 
				       job_id_ptr[i], node_name);  /* All is well */
			else {
				error ("REGISTERED JOB_ID %u ON WRONG NODE %s ", 
				       job_id_ptr[i], node_name);   /* Very bad */
				signal_job_on_node (job_id_ptr[i], step_id_ptr[i], 
							SIGKILL, node_name);
			}
		}

		else if (job_ptr->job_state == JOB_PENDING) {
			/* FIXME: In the future try to let job run */
			error ("REGISTERED PENDING JOB_ID %u ON NODE %s ", 
			       job_id_ptr[i], node_name);   /* Very bad */
			job_ptr->job_state = JOB_FAILED;
			last_job_update = time (NULL);
			job_ptr->end_time = time(NULL);
			delete_job_details(job_ptr);
			signal_job_on_node (job_id_ptr[i], step_id_ptr[i], 
						SIGKILL, node_name);
		}

		else {	/* else job is supposed to be done */
			error ("Registered job_id %u in state %s on node %s ", 
				job_id_ptr[i], 
			        job_state_string (job_ptr->job_state), node_name);
			signal_job_on_node (job_id_ptr[i], step_id_ptr[i], 
						SIGKILL, node_name);
			/* We may well have pending purge job RPC to send slurmd,
			 * which would synchronize this */
		}
	}
	return;
}

/* signal_job_on_node - send specific signal to specific job_id, step_id and node_name */
void
signal_job_on_node (uint32_t job_id, uint16_t step_id, int signum, char *node_name)
{
	/* FIXME: add code to send RPC to specified node */
	debug ("Signal %d send to job %u.%u on node %s",
		signum, job_id, step_id, node_name);
	error ("CODE DEVELOPMENT NEEDED HERE");
}


/* old_job_info - get details about an existing job allocation */
int
old_job_info (uint32_t uid, uint32_t job_id, char **node_list, 
	uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps)
{
	struct job_record *job_ptr;

	job_ptr = find_job_record (job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;
	if ((uid != 0) && (job_ptr->user_id != uid))
		return ESLURM_ACCESS_DENIED;
	if ((job_ptr->job_state != JOB_STAGE_IN) && 
	    (job_ptr->job_state != JOB_RUNNING))
		return ESLURM_ALREADY_DONE;

	node_list[0]      = job_ptr->nodes;
	*num_cpu_groups   = job_ptr->num_cpu_groups;
	cpus_per_node[0]  = job_ptr->cpus_per_node;
	cpu_count_reps[0] = job_ptr->cpu_count_reps;
	return SLURM_SUCCESS;
}


