/*****************************************************************************\
 *  step_launch.h - launch a parallel job step
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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
#ifndef _STEP_LAUNCH_H
#define _STEP_LAUNCH_H

#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/bitstring.h"
#include "src/common/eio.h"
#include "src/interfaces/mpi.h"
#include "src/common/slurm_step_layout.h"

#include "src/api/step_io.h"

struct step_launch_state {
	/* This lock protects tasks_started, tasks_exited, node_io_error,
	   io_deadline, abort, and abort_action_taken.  The main thread
	   blocks on cond, waking when a tast starts or exits, or the abort
	   flag is set. */
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int tasks_requested;
	bitstr_t *tasks_started; /* or attempted to start, but failed */
	bitstr_t *tasks_exited;  /* or never started correctly */
	bitstr_t *node_io_error;      /* set after write or read error */
	pthread_t io_timeout_thread;
	bool	  io_timeout_thread_created;
	time_t   *io_deadline;  /* Holds the time by which a "connection okay"
				   message must be received.  Each entry holds
				   NO_VAL unless the node is suspected to be
				   down and is being tested. */
	int	 io_timeout;    /* num seconds between I/O tests */
	bool	 halt_io_test;  /* set to true when I/O test thread should
				   shut down. */
	bool abort;
	bool abort_action_taken;

	/* message thread variables */
	eio_handle_t *msg_handle;
	pthread_t msg_thread;
	/* set to -1 if step launch message handler should not attempt
	   to handle */
	int slurmctld_socket_fd;
	uint16_t num_resp_port;
	uint16_t *resp_port; /* array of message response ports */

	/* io variables */
	client_io_t *io;

	slurm_step_layout_t *layout; /* a pointer into the ctx
					step_resp, do not free */
	mpi_step_info_t mpi_step[1];
	mpi_plugin_client_state_t *mpi_state;
	int ret_code;

	/* user registered callbacks */
	slurm_step_launch_callbacks_t callback;
};
typedef struct step_launch_state step_launch_state_t;


/*
 * Create a launch state structure for a specified step context, "ctx".
 */
struct step_launch_state * step_launch_state_create(slurm_step_ctx_t *ctx);

/*
 * If a steps size has changed update the launch_state structure for a
 * specified step context, "ctx".
 */
void step_launch_state_alter(slurm_step_ctx_t *ctx);

/*
 * Free the memory associated with the a launch state structure.
 */
void step_launch_state_destroy(struct step_launch_state *sls);

/*
 * Notify the step_launch_state that an I/O connection went bad.
 * If the node is suspected to be down, abort the job.
 */
int step_launch_notify_io_failure(step_launch_state_t *sls, int node_id);

/*
 * Just in case the node was marked questionable very early in the
 * job step setup, clear this flag when the node makes its initial
 * connection.
 */
int step_launch_clear_questionable_state(step_launch_state_t *sls,
					 int node_id);


#endif /* _STEP_LAUNCH_H */
