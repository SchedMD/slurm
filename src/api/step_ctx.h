/*****************************************************************************\
 *  step_ctx.h - step context declarations
 *
 *  $Id: spawn.c 8334 2006-06-07 20:36:04Z morrone $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>,
 *  Christopher J. Morrone <morrone2@llnl.gov>
 *  UCRL-CODE-217948.
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

#include <unistd.h>
#include <stdint.h>
#include <pthread.h>

#include <slurm/slurm.h>

#include "src/common/slurm_step_layout.h"
#include "src/common/eio.h"
#include "src/common/bitstring.h"

#include "src/api/step_io.h"

#define STEP_CTX_MAGIC 0xc7a3

struct step_launch_state {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int tasks_requested;
	bitstr_t *tasks_started; /* or attempted to start, but failed */
	bitstr_t *tasks_exited;  /* or never started correctly */

	/* message thread variables */
	eio_handle_t *msg_handle;
	pthread_t msg_thread;
	uint16_t num_resp_port;
	uint16_t *resp_port; /* array of message response ports */

	/* client side io variables */
	client_io_t *client_io;
	slurm_step_layout_t *layout; /* a pointer into the ctx
					step_resp, do not free */

	/* user registered callbacks */
	void (*task_start_callback)(launch_tasks_response_msg_t *);
	void (*task_finish_callback)(task_exit_msg_t *);
};

struct slurm_step_ctx_struct {
	uint16_t magic;	/* magic number */

	uint32_t job_id;	/* assigned job id */
	uint32_t user_id;	/* user the job runs as */
	
	job_step_create_request_msg_t *step_req;
	job_step_create_response_msg_t *step_resp;

	char *cwd;		/* working directory */
	uint32_t argc;		/* count of arguments */
	char **argv;		/* argument list */
	uint16_t env_set;	/* flag if user set env */
	uint32_t envc;		/* count of env vars */
	char **env;		/* environment variables */

	/* Used by slurm_step_launch(), but not slurm_spawn() */
	struct step_launch_state *launch_state;
	int slurmctld_socket_fd; /* set to -1 if slaunch message handler
				    should not attempt to handle */
};
