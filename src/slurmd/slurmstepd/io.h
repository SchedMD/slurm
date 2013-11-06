/*****************************************************************************\
 * src/slurmd/slurmstepd/io.h - slurmstepd standard IO routines
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _IO_H
#define _IO_H

#include "src/common/eio.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * The message cache uses up free message buffers, so STDIO_MAX_MSG_CACHE
 * must be a number smaller than STDIO_MAX_FREE_BUF.
 */
#define STDIO_MAX_FREE_BUF 1024
#define STDIO_MAX_MSG_CACHE 128

struct io_buf {
	int ref_count;
	uint32_t length;
	void *data;
};

/* For each task's ofname and efname, are all the names NULL,
   one null and the others "/dev/null", all non-null and unique,
   or all non-null and identical. */
typedef enum {
	SLURMD_ALL_NULL,   /* output from all tasks goes to the client (srun) */
	SLURMD_ONE_NULL,   /* output from one task goes to the client, output
			      from other tasks is discarded */
	SLURMD_ALL_UNIQUE, /* separate output files per task.  written from
			      tasks unless stepd_step_rec_t->labelio == true, in
			      which case the slurmstepd does the write */
	SLURMD_ALL_SAME,   /* all tasks write to the same file.  written from
			      tasks unless stepd_step_rec_t->labelio == true, in
			      which case the slurmstepd does the write */
	SLURMD_UNKNOWN
} slurmd_filename_pattern_t;


struct io_buf *alloc_io_buf(void);
void free_io_buf(struct io_buf *buf);

/*
 * Create a TCP connection back the initial client (e.g. srun).
 *
 * Since this is the first client connection and the IO engine has not
 * yet started, we initialize the msg_queue as an empty list and
 * directly add the eio_obj_t to the eio handle with eio_new_initial_handle.
 */
int io_initial_client_connect(srun_info_t *srun, stepd_step_rec_t *job,
			      int stdout_tasks, int stderr_tasks);

/*
 * Initiate a TCP connection back to a waiting client (e.g. srun).
 *
 * Create a new eio client object and wake up the eio engine so that
 * it can see the new object.
 */
int io_client_connect(srun_info_t *srun, stepd_step_rec_t *job);


/*
 * Open a local file and create and eio object for files written
 * from the slurmstepd, probably with labelled output.
 */
int
io_create_local_client(const char *filename, int file_flags,
		       stepd_step_rec_t *job, bool labelio,
		       int stdout_tasks, int stderr_tasks);

/*
 * Initialize each task's standard I/O file descriptors.  The file descriptors
 * may be files, or may be the end of a pipe which is handled by an eio_obj_t.
 */
int io_init_tasks_stdio(stepd_step_rec_t *job);

/*
 * Start IO handling thread.
 * Initializes IO pipes, creates IO objects and appends them to job->objs,
 * and opens 2*ntask initial connections for stdout/err, also appending these
 * to job->objs list.
 */
int io_thread_start(stepd_step_rec_t *job);

int io_dup_stdio(stepd_step_task_info_t *t);

/*
 *  Close the tasks' ends of the stdio pipes.
 *  Presumably the tasks have already been started, and
 *  have their copies of these file descriptors.
 */
void io_close_task_fds(stepd_step_rec_t *job);

void io_close_all(stepd_step_rec_t *job);

void io_close_local_fds(stepd_step_rec_t *job);


/*
 *  Look for a pattern in the stdout and stderr file names, and see
 *  if stdout and stderr point to the same file(s).
 *  See comments above for slurmd_filename_pattern_t.
 */
void io_find_filename_pattern(  stepd_step_rec_t *job,
				slurmd_filename_pattern_t *outpattern,
				slurmd_filename_pattern_t *errpattern,
				bool *same_out_err_files );

/*
 *  Get the flags to be used with the open call to create output files.
 */
int io_get_file_flags(stepd_step_rec_t *job);

/*
 *  Initialize "user managed" IO, where each task has a single TCP
 *  socket end point shared on stdin, stdout, and stderr.
 */
int user_managed_io_client_connect(int ntasks, srun_info_t *srun,
				   stepd_step_task_info_t **tasks);

#endif /* !_IO_H */
