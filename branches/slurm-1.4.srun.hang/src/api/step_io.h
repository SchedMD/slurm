/*****************************************************************************\
 * src/api/step_io.h - job-step client-side I/O routines
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
#ifndef _HAVE_STEP_IO_H
#define _HAVE_STEP_IO_H

#include <stdint.h>
#include <pthread.h>

#include <slurm/slurm.h>

#include "src/common/eio.h"
#include "src/common/list.h"
#include "src/common/bitstring.h"
#include "src/common/slurm_step_layout.h"
struct step_launch_state;


struct client_io {
	/* input parameters - set (indirectly) by user */
	int num_tasks;
	int num_nodes;
	bool label;
	int label_width;
	char *io_key;

	/* internal variables */
	pthread_t ioid;		/* stdio thread id 		  */
	int num_listen;		/* Number of stdio listen sockets */
	int *listensock;	/* Array of stdio listen sockets  */
	uint16_t *listenport;	/* Array of stdio listen port numbers */

	eio_handle_t *eio;      /* Event IO handle for stdio traffic */
	pthread_mutex_t ioservers_lock; /* This lock protects 
				   ioservers_ready_bits, ioservers_ready, 
				   pointers in ioserver, all the msg_queues
				   in each ioserver's server_io_info, and 
				   the free_incoming list.  The queues
				   are used both for normal writes
				   and writes that verify a connection to
				   a remote host. */
	bitstr_t *ioservers_ready_bits; /* length "num_nodes" */
	int ioservers_ready;    /* Number of servers that established contact */
	eio_obj_t **ioserver;	/* Array of nhosts pointers to eio_obj_t */
	eio_obj_t *stdin_obj;
	eio_obj_t *stdout_obj;
	eio_obj_t *stderr_obj;
	List free_incoming;     /* List of free struct io_buf * for incoming
				 * traffic. "incoming" means traffic from the
				 * client to the tasks.
				 */
	List free_outgoing;     /* List of free struct io_buf * for outgoing
				 * traffic "outgoing" means traffic from the
				 * tasks to the client.
				 */
	int incoming_count;     /* Count of total incoming message buffers
			         * including free_incoming buffers and
			         * buffers in use.
			         */
	int outgoing_count;     /* Count of total incoming message buffers
			         * including free_incoming buffers and
			         * buffers in use.
			         */

	struct step_launch_state *sls; /* Used to notify the main thread of an
				       I/O problem.  */
};

typedef struct client_io client_io_t;


/*
 * IN cred - cred need not be a real job credential, it may be a "fake"
 *	credential generated with slurm_cred_faker().  The credential is
 *	sent to the slurmstepd (via the slurmd) which generates a signature
 *	string from the credential.  The slurmstepd sends the signature back
 *	back to the client when it establishes the IO connection as a sort
 *	of validity check.
 */
client_io_t *client_io_handler_create(slurm_step_io_fds_t fds,
				      int num_tasks,
				      int num_nodes,
				      slurm_cred_t *cred,
				      bool label);

int client_io_handler_start(client_io_t *cio);

/*
 * Tell the client IO handler that a set of remote nodes are now considered
 * "down", and no further communication from that node should be expected.
 * This will prevent the IO handler from blocking indefinitely while it
 * waits for a node to phone home.
 *
 * IN cio - the client_io_t handle
 * IN node_ids - an array of integers representing the ID of a node
 *               within a job step.
 * IN num_node_ids - the length of the node_ids array
 */
void client_io_handler_downnodes(client_io_t *cio,
				 const int *node_ids, int num_node_ids);

/*
 * Tell the client IO handler to test the communication path to a 
 * node suspected to be down by sending a message, which will be
 * ignored by the slurmstepd.  If the write fails the step_launch_state
 * will be notified.
 */
int client_io_handler_send_test_message(client_io_t *cio, int node_id, 
					bool *sent_message);

/*
 * Tell the client IO handler that the step has been aborted, and if
 * any slurmstepd's have not yet establish IO connections, they should
 * not be expected to ever make a connection.
 *
 * Calling this when an error occurs will prevent client_io_handler_finish()
 * from blocking indefinitely.
 *
 * WARNING: This WILL abandon live IO connections.
 */
void client_io_handler_abort(client_io_t *cio);

int client_io_handler_finish(client_io_t *cio);

void client_io_handler_destroy(client_io_t *cio);

#endif /* !_HAVE_STEP_IO_H */
